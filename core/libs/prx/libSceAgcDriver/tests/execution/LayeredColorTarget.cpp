#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorTargetLayout.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderInputState.hpp"
#include "prx/libSceAgcDriver/Graphics/include/State.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::ColorTargetLayout;
using AgcDriver::Graphics::ColorTileMode;
using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Width = 256;
constexpr std::uint32_t Height = 128;
constexpr std::uint32_t Slices = 4;
constexpr std::uint32_t FirstSlice = 1;
constexpr std::uint32_t LastSlice = 2;
constexpr std::size_t BlockBytes = 65536;
constexpr std::uint32_t Float32Info = (4u << 2u) | (7u << 8u);
constexpr std::uint32_t TiledAttrib3 = 0x4dc6c000u | (Slices - 1u);
constexpr std::uint32_t VolumeAttrib3 = 0x4ec6c000u | (Slices - 1u);
constexpr std::uint32_t Float32Export = 9;
constexpr std::uint32_t LayerOutControl = (1u << 18u) | (1u << 21u);

alignas(256) constexpr std::array<std::uint32_t, 10> VertexCode{
    0xe0382000, 0x80000005, 0xbf8c3f70, 0x7e0a0f02, 0x7e0402f0, 0xf80000cf, 0x03020100, 0xf80008d4, 0x00050000, 0xbf810000,
};

alignas(256) std::array<std::array<std::uint32_t, 16>, 16> Programs{};
std::size_t programCount = 0;

std::span<const std::uint32_t> ConstantProgram(float value) {
    Require(programCount < Programs.size(), "too many test programs");
    auto& code = Programs[programCount++];
    std::size_t at = 0;
    for (std::uint32_t vgpr = 0; vgpr < 4u; ++vgpr) {
        code[at++] = 0x7e0002ffu | ((4u + vgpr) << 17u);
        code[at++] = std::bit_cast<std::uint32_t>(vgpr == 0 ? value : 1.0f);
    }
    code[at++] = 0xf800180fu;
    code[at++] = 0x07060504u;
    code[at++] = 0xbf810000u;
    return std::span<const std::uint32_t>(code.data(), at);
}

struct Block {
    explicit Block(std::size_t bytes) : bytes(bytes), watched(AgcDriver::GuestMemory::WriteWatched()) {
#ifdef _WIN32
        if (watched) {
            data = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, BlockBytes));
            GuestArena::GuestArenaCommit_nid_postfix(data, bytes, PAGE_READWRITE, bytes);
        } else {
            data = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        }
#else
        if (watched) {
            void* raw = mmap(nullptr, bytes + BlockBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            Require(raw != MAP_FAILED, "cannot map the color target");
            const auto begin = reinterpret_cast<std::uintptr_t>(raw);
            const auto aligned = (begin + BlockBytes - 1) & ~(static_cast<std::uintptr_t>(BlockBytes) - 1);
            if (aligned != begin) munmap(raw, aligned - begin);
            if (aligned + bytes != begin + bytes + BlockBytes) munmap(reinterpret_cast<void*>(aligned + bytes), begin + BlockBytes - aligned);
            data = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(data, bytes);
        } else {
            data = static_cast<std::uint8_t*>(std::aligned_alloc(BlockBytes, bytes));
        }
#endif
        Require(data != nullptr, "cannot allocate the color target");
        GuestAllocations::Mutation mutation;
        mutation.Add(data, bytes, true, true, true);
    }
    ~Block() {
        AgcDriver::Graphics::StorageTexture::FlushPending(Address(), bytes, nullptr, "test release");
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(data);
        }
#ifdef _WIN32
        if (watched) {
            GuestArena::GuestArenaReset_nid_postfix(data, bytes);
            GuestArena::GuestArenaRelease_nid_postfix(data, bytes);
        } else {
            VirtualFree(data, 0, MEM_RELEASE);
        }
#else
        if (watched) {
            munmap(data, bytes);
            GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(data, bytes);
        } else {
            std::free(data);
        }
#endif
    }
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    std::uint64_t Address() const { return reinterpret_cast<std::uintptr_t>(data); }
    std::size_t bytes;
    bool watched;
    std::uint8_t* data = nullptr;
};

AgcDriver::Registers TargetRegisters(std::uint64_t address, std::uint32_t firstSlice, std::uint32_t lastSlice, std::uint32_t attrib3 = TiledAttrib3) {
    AgcDriver::Registers cx;
    cx[0x318] = static_cast<std::uint32_t>(address >> 8u);
    cx[0x31b] = firstSlice | (lastSlice << 13u);
    cx[0x31c] = Float32Info;
    cx[0x31d] = 0;
    cx[0x3b0] = ((Width - 1u) << 14u) | (Height - 1u);
    cx[0x3b8] = attrib3;
    cx[0x390] = static_cast<std::uint32_t>(address >> 40u);
    return cx;
}

void Draw(AgcDriver::VulkanDevice& device, const AgcDriver::Graphics::ColorTarget& color, float layer, float value, std::uint32_t outControl = LayerOutControl) {
    const auto target = device.Target();
    constexpr std::uint32_t waveSize = 64;
    static std::array<std::array<float, 4>, 3> triangle{};
    triangle = {{{-1.0f, -1.0f, layer, 1.0f}, {3.0f, -1.0f, layer, 1.0f}, {-1.0f, 3.0f, layer, 1.0f}}};
    std::vector<std::uint32_t> vertexUserData(4, 0u);
    const auto address = reinterpret_cast<std::uintptr_t>(triangle.data());
    const std::array<std::uint32_t, 4> vertexBuffer{static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (16u << 16u), static_cast<std::uint32_t>(triangle.size()), 0x01016facu};
    std::copy(vertexBuffer.begin(), vertexBuffer.end(), vertexUserData.begin());
    const std::array<ShaderRecompiler::MemoryRegion, 1> vertexMemory{{{reinterpret_cast<std::uintptr_t>(VertexCode.data()), std::as_bytes(std::span(VertexCode))}}};
    ShaderRecompiler::ShaderVertexStageInfo vertexInfo{};
    vertexInfo.paClVsOutCntl = outControl;
    ShaderRecompiler::RecompileRequest vertex{
        {ShaderStage::Vertex, reinterpret_cast<std::uintptr_t>(VertexCode.data()), VertexCode, 0, {}},
        {waveSize, 0, vertexUserData, std::nullopt, std::nullopt, vertexInfo, vertexMemory},
        target,
        {0, 0, 0, 64}
    };
    vertex.useCache = false;
    const auto vertexResult = ShaderRecompiler::Recompile(vertex);
    const auto vertexPush = static_cast<std::uint32_t>(vertexResult.pushConstants.size());

    AgcDriver::Graphics::State state{};
    state.stages = {AgcDriver::Graphics::ShaderPath::Vertex, 0u, waveSize, waveSize, std::nullopt, std::nullopt};
    state.colors = {color};
    state.color = color;
    state.hasColorTarget = true;
    state.vertexOutControl = outControl;
    state.renderExtent = {Width, Height};
    state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    state.viewport = {0, static_cast<float>(Height), static_cast<float>(Width), -static_cast<float>(Height), 0, 1};
    state.negativeOneToOne = false;
    state.scissor = {{0, 0}, {Width, Height}};
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    state.blends = {blend};
    state.blend = blend;
    state.blendConstants = {};

    const auto pixelCode = ConstantProgram(value);
    const auto codeAddress = reinterpret_cast<std::uintptr_t>(pixelCode.data());
    const AgcDriver::Registers context{{0x1b6, 0x8000}, {0x1b3, 0}, {0x1b4, 0}, {0x203, 0}, {0x1c5, Float32Export}};
    const std::array<ShaderRecompiler::MemoryRegion, 1> pixelMemory{{{codeAddress, std::as_bytes(pixelCode)}}};
    ShaderRecompiler::RecompileRequest fragment{
        {ShaderStage::Fragment, codeAddress, pixelCode, 0, {}},
        {waveSize, 0, {}, std::nullopt, AgcDriver::Graphics::DecodePixelStageInfo(context, AgcDriver::Graphics::ExportMappings(state)), std::nullopt, pixelMemory},
        target,
        PixelPushLayout(vertexPush, target)
    };
    fragment.useCache = false;
    const auto pixelResult = ShaderRecompiler::Recompile(fragment);
    const std::array<AgcDriver::Graphics::CompiledShader, 2> shaders{{
        {ShaderStage::Vertex, &vertexResult, 0},
        {ShaderStage::Fragment, &pixelResult, PixelPushOffset(vertexPush, target)}
    }};
    const AgcDriver::Pm4::DrawParameters draw{0, static_cast<std::uint32_t>(triangle.size()), 0, 1, 0, false};
    device.Draw(state, draw, shaders);
}

std::vector<float> ReadBack(AgcDriver::VulkanDevice& device, const Block& block) {
    AgcDriver::Graphics::StorageTexture::FlushPending(block.Address(), block.bytes, nullptr, "test read-back");
    device.WaitIdle();
    std::vector<float> stored(block.bytes / 4u);
    AgcDriver::GuestMemory::Read(block.Address(), std::as_writable_bytes(std::span(stored)), 1);
    return stored;
}

void ExpectSlices(const std::vector<float>& stored, const std::vector<float>& before, const ColorTargetLayout& layout, std::uint64_t layerBytes, const std::array<float, Slices>& expected, const std::array<bool, Slices>& written, const std::string& what) {
    for (std::uint32_t slice = 0; slice < Slices; ++slice) {
        for (std::uint32_t y = 0; y < Height; ++y) {
            for (std::uint32_t x = 0; x < Width; ++x) {
                const auto index = static_cast<std::size_t>((slice * layerBytes + layout.Offset(x, y)) / 4u);
                const auto want = written[slice] ? expected[slice] : before[index];
                if (std::bit_cast<std::uint32_t>(stored[index]) == std::bit_cast<std::uint32_t>(want)) continue;
                char detail[160];
                std::snprintf(detail, sizeof(detail), ": slice %u pixel (%u, %u) holds %g, expected %g", slice, x, y, stored[index], want);
                throw std::runtime_error(what + detail);
            }
        }
    }
}

void Mark(Block& block) {
    for (std::size_t word = 0; word < block.bytes / 4u; ++word) {
        const float marker = -1.0f - static_cast<float>(word % 7u);
        std::memcpy(block.data + word * 4u, &marker, 4);
    }
}

void VolumeTests(AgcDriver::VulkanDevice& device) {
    Block block(BlockBytes * 64);
    for (std::uint32_t layer = 0; layer < 2; ++layer) {
        Mark(block);
        const auto marked = ReadBack(device, block);
        const auto layered = AgcDriver::Graphics::DecodeColorBuffer(TargetRegisters(block.Address(), FirstSlice, LastSlice, VolumeAttrib3), 0);
        Require(layered.depth == Slices && layered.layers == LastSlice - FirstSlice + 1u && layered.baseLayer == FirstSlice, "the 3D view did not decode its slice range");
        Draw(device, layered, static_cast<float>(layer), 0.5f);
        const auto throughLayers = ReadBack(device, block);
        Mark(block);
        static_cast<void>(ReadBack(device, block));
        const auto single = AgcDriver::Graphics::DecodeColorBuffer(TargetRegisters(block.Address(), FirstSlice + layer, FirstSlice + layer, VolumeAttrib3), 0);
        Require(single.layers == 1 && single.depthSlice == FirstSlice + layer, "the one-slice 3D view did not decode its slice");
        Draw(device, single, 0.0f, 0.5f, 0);
        const auto throughSlice = ReadBack(device, block);
        Require(throughSlice != marked, "a draw into one 3D slice wrote nothing");
        Require(throughLayers == throughSlice, "a draw exporting layer " + std::to_string(layer) + " of a 3D view differs from a draw into that slice alone");
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const ColorTargetLayout layout(Width, Height, ColorTileMode::RenderTarget, 4);
        Block probe(BlockBytes * 64);
        const auto single = AgcDriver::Graphics::DecodeColorBuffer(TargetRegisters(probe.Address(), 1, 1), 0);
        Require(single.layers == 1 && single.layerBytes >= layout.Bytes() && single.address == probe.Address() + single.layerBytes, "a one-slice view of an array target did not decode as one slice");
        const auto layerBytes = single.layerBytes;
        Block block(static_cast<std::size_t>(layerBytes * Slices));
        const auto color = AgcDriver::Graphics::DecodeColorBuffer(TargetRegisters(block.Address(), FirstSlice, LastSlice), 0);
        Require(color.format == VK_FORMAT_R32_SFLOAT, "the 32-bit float target did not decode as R32_SFLOAT");
        Require(color.layers == LastSlice - FirstSlice + 1u && color.baseLayer == FirstSlice && color.arrayLayers == Slices && color.arrayAddress == block.Address(), "the two-slice view did not decode its slice range");
        Require(color.address == block.Address() + FirstSlice * layerBytes && color.bytes == layerBytes + layout.Bytes(), "the two-slice view does not span its slices");
        for (std::uint32_t slice = 0; slice < Slices; ++slice) {
            for (std::uint32_t word = 0; word < layerBytes / 4u; ++word) {
                const float marker = -1.0f - static_cast<float>(slice);
                std::memcpy(block.data + slice * layerBytes + word * 4u, &marker, 4);
            }
        }
        const auto before = ReadBack(*device, block);
        Draw(*device, color, 1.0f, 0.75f);
        const auto first = ReadBack(*device, block);
        ExpectSlices(first, before, layout, layerBytes, {0.0f, 0.0f, 0.75f, 0.0f}, {false, false, true, false}, "a draw exporting layer 1 of a view starting at slice 1");
        Draw(*device, color, 0.0f, 0.25f);
        const auto second = ReadBack(*device, block);
        ExpectSlices(second, first, layout, layerBytes, {0.0f, 0.25f, 0.0f, 0.0f}, {false, true, false, false}, "a draw exporting layer 0 of a view starting at slice 1");
        VolumeTests(*device);
        std::puts("layered color target tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
