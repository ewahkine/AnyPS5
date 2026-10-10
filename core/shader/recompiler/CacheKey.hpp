#ifndef CORE_SHADER_RECOMPILER_CACHEKEY_HPP
#define CORE_SHADER_RECOMPILER_CACHEKEY_HPP

#include "Recompiler.hpp"
#include "RdnaDecoder/RdnaDescriptorFormat.hpp"
#include "prx/libc/include/HostThreadLocal.hpp"
#include <algorithm>
#include <cstdlib>
#include <span>
#include <string_view>
#include <stdexcept>
#include <type_traits>

namespace ShaderRecompiler {

class RecompileCacheKey {
public:
    static void Build(const RecompileRequest& request, std::vector<std::uint64_t>& key) {
        key.clear();
        append(key, RuntimeAbi::Version);
        append(key, request.shader.stage);
        // The code enters as a hash rather than word by word: the key is built, hashed and compared
        // on every dispatch and draw. The cache verifies a match against the code it stored.
        // Debug aid: APS5_NO_CODE_HASH_KEY=1 appends every code word, as before.
        static const bool hashedCode = std::getenv("APS5_NO_CODE_HASH_KEY") == nullptr;
        if (hashedCode) {
            append(key, request.shader.code.size());
            append(key, HashCode(request.shader.code));
        } else {
            append(key, request.shader.code);
        }
        appendInterface(key, request);
    }

    static void BuildInterface(const RecompileRequest& request, std::vector<std::uint64_t>& key) {
        key.clear();
        append(key, RuntimeAbi::Version);
        append(key, request.shader.stage);
        appendInterface(key, request);
    }

    static std::string_view InterfaceDifference(const RecompileRequest& request, std::span<const std::uint64_t> other) {
        std::vector<std::uint64_t> key;
        std::string_view difference;
        const auto mark = [&](std::string_view field) {
            if (!difference.empty()) return;
            if (key.size() > other.size() || !std::ranges::equal(key, other.first(key.size()))) difference = field;
        };
        append(key, RuntimeAbi::Version);
        mark("runtime ABI");
        append(key, request.shader.stage);
        mark("stage");
        appendInterface(key, request, mark);
        return difference;
    }

    // A hash over every field Build appends except the code, the target and the probe flag: the
    // key of a source memo whose owner fixes the code (a registered shader at an offset) and the
    // device itself, and which is bypassed while the probe is active.
    static std::uint64_t ContextHash(const RecompileRequest& request) {
        struct ContextKeyStorage {};
        auto& key = HostThreadLocal<std::vector<std::uint64_t>, ContextKeyStorage>();
        key.clear();
        append(key, RuntimeAbi::Version);
        append(key, request.shader.stage);
        append(key, request.context.waveSize);
        append(key, request.context.userDataBaseRegister);
        append(key, request.context.userData.size());
        append(key, request.context.compute);
        append(key, request.context.pixel);
        append(key, request.context.vertex);
        append(key, request.context.floatMode);
        appendMesh(key, request);
        appendTessellation(key, request);
        std::uint64_t hash = 0xcbf29ce484222325ull;
        for (const auto value : key) {
            hash ^= value;
            hash *= 0x100000001b3ull;
        }
        return hash;
    }

    // A 64-bit hash of the code, two dwords per step; collisions are resolved by comparing the code.
    static std::uint64_t HashCode(std::span<const std::uint32_t> code) {
        std::uint64_t hash = 0x9e3779b97f4a7c15ull ^ (static_cast<std::uint64_t>(code.size()) * 0x100000001b3ull);
        const auto mix = [&](std::uint64_t chunk) {
            hash = (hash ^ chunk) * 0x9e3779b97f4a7c15ull;
            hash ^= hash >> 29u;
        };
        std::size_t index = 0;
        for (; index + 2 <= code.size(); index += 2) mix(code[index] | (static_cast<std::uint64_t>(code[index + 1]) << 32u));
        if (index < code.size()) mix(code[index]);
        return hash;
    }

private:
    static void appendInterface(std::vector<std::uint64_t>& key, const RecompileRequest& request) {
        appendInterface(key, request, [](std::string_view) {});
    }

    template<typename TMark>
    static void appendInterface(std::vector<std::uint64_t>& key, const RecompileRequest& request, TMark mark) {
        append(key, request.context.waveSize);
        mark("wave size");
        append(key, request.context.userDataBaseRegister);
        append(key, request.context.userData.size());
        mark("user data");
        append(key, request.context.compute);
        mark("compute stage");
        append(key, request.context.pixel);
        mark("pixel stage");
        append(key, request.context.vertex.has_value());
        mark("vertex stage presence");
        if (request.context.vertex) appendVertex(key, *request.context.vertex, mark);
        append(key, request.context.floatMode);
        mark("float mode");
        appendMesh(key, request);
        mark("mesh state");
        appendTessellation(key, request);
        mark("tessellation state");
        appendTarget(key, request.target, mark);
        append(key, DebugProbeActive());
        append(key, RayTracingStrict());
        append(key, RayTracingMiss());
        mark("debug switches");
    }

    static void appendMesh(std::vector<std::uint64_t>& key, const RecompileRequest& request) {
        if (request.shader.stage != ShaderStage::Mesh) return;
        const auto* mesh = request.graphics && request.graphics->mesh ? &*request.graphics->mesh : nullptr;
        append(key, mesh != nullptr);
        if (mesh == nullptr) return;
        for (const auto value : {mesh->inputPrimitive, mesh->primitivesPerGroup, mesh->verticesPerGroup, mesh->maxVertices, mesh->maxPrimitives, mesh->threadsPerGroup, mesh->ldsSizeDwords, mesh->provokingVertex, mesh->esgsItemSize}) append(key, value);
    }

    static void appendTessellation(std::vector<std::uint64_t>& key, const RecompileRequest& request) {
        if (request.shader.stage != ShaderStage::Local && request.shader.stage != ShaderStage::TessellationControl && request.shader.stage != ShaderStage::TessellationEvaluation) return;
        const auto* tessellation = request.graphics && request.graphics->tessellation ? &*request.graphics->tessellation : nullptr;
        append(key, tessellation != nullptr);
        if (tessellation == nullptr) return;
        for (const auto value : {tessellation->inputControlPoints, tessellation->outputControlPoints, tessellation->domain, tessellation->partitioning, tessellation->outputTopology}) append(key, value);
    }

    template<typename TValue>
    static void append(std::vector<std::uint64_t>& key, TValue value) requires (std::is_integral_v<TValue> || std::is_enum_v<TValue>) {
        key.push_back(static_cast<std::uint64_t>(value));
    }

    template<typename TValue, std::size_t TSize>
    static void append(std::vector<std::uint64_t>& key, const std::array<TValue, TSize>& values) {
        for (const auto value : values) append(key, value);
    }

    template<typename TValue>
    static void append(std::vector<std::uint64_t>& key, std::span<TValue> values) {
        append(key, values.size());
        for (const auto value : values) append(key, value);
    }

    template<typename TValue>
    static void append(std::vector<std::uint64_t>& key, const std::optional<TValue>& value) {
        append(key, value.has_value());
        if (value) append(key, *value);
    }

    static void append(std::vector<std::uint64_t>& key, std::string_view value) {
        append(key, value.size());
        for (const unsigned char byte : value) append(key, byte);
    }

    static void append(std::vector<std::uint64_t>& key, const ShaderFloatMode& value) {
        append(key, value.floatMode);
        append(key, value.dx10Clamp);
        append(key, value.ieeeMode);
        append(key, value.fp16Overflow);
    }

    static void append(std::vector<std::uint64_t>& key, const ShaderComputeStageInfo& value) {
        append(key, value.numThreads);
        append(key, value.ldsSizeDwords);
        append(key, value.groupIdEnable);
        append(key, value.tgSizeEnable);
        append(key, value.threadIdComponentCount);
        append(key, value.PartialGroups());
        append(key, value.scratchDwords);
    }

    static void append(std::vector<std::uint64_t>& key, const ShaderPixelStageInfo& value) {
        append(key, value.interpolatorCount);
        if (value.interpolatorCount > value.interpolatorSettings.size()) throw std::runtime_error("Shader cache: invalid interpolator count");
        for (std::uint32_t i = 0; i < value.interpolatorCount; ++i) append(key, value.interpolatorSettings[i]);
        append(key, value.wave32);
        append(key, value.inputAddr);
        append(key, value.hasPerspectiveCenterVgpr);
        append(key, value.perspectiveCentroid);
        append(key, value.posX);
        append(key, value.posY);
        append(key, value.posZ);
        append(key, value.posW);
        append(key, value.frontFace);
        append(key, value.ancillary);
        append(key, value.sampleShading);
        append(key, value.noPerspective);
        append(key, value.linearCentroid);
        append(key, value.pixelKillEnable);
        append(key, value.depthExportEnable);
        append(key, value.sampleMaskExportEnable);
        append(key, value.earlyZ);
        append(key, value.executeOnNoop);
        append(key, value.conservativeZExport);
        append(key, value.orderedPixelShader);
        append(key, value.targetOutputMode);
    }

    static void append(std::vector<std::uint64_t>& key, const ShaderVertexResourceDestination& value) {
        append(key, value.registerStart);
        append(key, value.registersNum);
        append(key, value.attrId);
        append(key, value.fetchIndex);
    }

    static void append(std::vector<std::uint64_t>& key, const ShaderVertexStageInfo& value) {
        appendVertex(key, value, [](std::string_view) {});
    }

    template<typename TMark>
    static void appendVertex(std::vector<std::uint64_t>& key, const ShaderVertexStageInfo& value, TMark mark) {
        append(key, value.fetchAttribReg);
        append(key, value.fetchBufferReg);
        append(key, value.fetchEmbedded);
        mark("vertex fetch registers");
        append(key, value.paClVsOutCntl);
        mark("vertex output control");
        if (value.resourcesNum > value.resources.size()) throw std::runtime_error("Shader cache: invalid vertex resource count");
        append(key, value.resourcesNum);
        for (std::uint32_t i = 0; i < value.resourcesNum; ++i) {
            if (!value.fetchEmbedded) append(key, VertexInputNumericClass(static_cast<IrBufferFormat>((value.resources[i].fields[3] >> 12u) & 0x7fu)));
            auto destination = value.resourcesDst[i];
            if (value.fetchEmbedded) destination.fetchIndex = 0;
            append(key, destination);
        }
        mark("vertex inputs");
    }

    static void append(std::vector<std::uint64_t>& key, const MeshTargetLimits& value) {
        append(key, value.maxWorkgroupSize);
        append(key, value.maxWorkgroupInvocations);
        append(key, value.maxSharedMemoryBytes);
        append(key, value.maxOutputVertices);
        append(key, value.maxOutputPrimitives);
        append(key, value.maxOutputComponents);
        append(key, value.maxOutputMemoryBytes);
        append(key, value.outputPerVertexGranularity);
        append(key, value.outputPerPrimitiveGranularity);
    }

    static void append(std::vector<std::uint64_t>& key, const TessellationTargetLimits& value) {
        append(key, value.maxPatchSize);
        append(key, value.maxControlPerVertexInputComponents);
        append(key, value.maxControlPerVertexOutputComponents);
        append(key, value.maxControlPerPatchOutputComponents);
        append(key, value.maxControlTotalOutputComponents);
        append(key, value.maxEvaluationInputComponents);
        append(key, value.maxEvaluationOutputComponents);
    }

    static void append(std::vector<std::uint64_t>& key, const SpirvTarget& value) {
        appendTarget(key, value, [](std::string_view) {});
    }

    template<typename TMark>
    static void appendTarget(std::vector<std::uint64_t>& key, const SpirvTarget& value, TMark mark) {
        append(key, value.vulkanVersion);
        append(key, value.spirvVersion);
        append(key, value.subgroupSize);
        append(key, value.bdaAbiVersion);
        mark("SPIR-V target versions");
        append(key, value.supportedCapabilities);
        append(key, value.supportedExtensions);
        mark("SPIR-V target capabilities");
        append(key, value.fragmentShaderBarycentricEnabled);
        append(key, value.maxWorkgroupSize);
        append(key, value.maxWorkgroupInvocations);
        append(key, value.maxWorkgroupSharedMemoryBytes);
        append(key, value.mesh);
        append(key, value.tessellation);
        mark("SPIR-V target limits");
        append(key, value.nonConstantImageOffsets);
        mark("SPIR-V target image offsets");
        append(key, value.srgbDecodeFormats);
        mark("SPIR-V target sRGB decode formats");
        append(key, value.narrowSubgroupClock);
        mark("SPIR-V target subgroup clock");
    }
};

}

#endif
