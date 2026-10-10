#include "GraphicsTests.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace AgcDriver::Graphics;

constexpr std::uint32_t FormatR8Uint = 5;
constexpr std::uint32_t FormatR16Unorm = 7;
constexpr std::uint32_t FormatR16Uint = 11;
constexpr std::uint32_t FormatR32Uint = 20;
constexpr std::uint32_t FormatR32Float = 22;
constexpr std::uint32_t FormatR11G11B10Float = 36;
constexpr std::uint32_t TileDepth64KB = 0x18;
constexpr std::uint32_t TileRenderTarget64KB = 0x1b;
constexpr std::uint32_t Type2D = 9;
constexpr std::uint32_t Type2DArray = 13;

std::uintptr_t nextHandle = 0x1000;

template<typename THandle>
THandle newHandle() {
    return reinterpret_cast<THandle>(nextHandle += 0x10);
}

VKAPI_ATTR VkResult VKAPI_CALL createImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage* image) {
    *image = newHandle<VkImage>();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL imageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements* requirements) {
    *requirements = {65536, 65536, 1};
}

VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* memory) {
    *memory = newHandle<VkDeviceMemory>();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL bindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) {
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL createImageView(VkDevice, const VkImageViewCreateInfo*, const VkAllocationCallbacks*, VkImageView* view) {
    *view = newHandle<VkImageView>();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL allocateCommandBuffers(VkDevice, const VkCommandBufferAllocateInfo*, VkCommandBuffer* commands) {
    *commands = newHandle<VkCommandBuffer>();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL createFence(VkDevice, const VkFenceCreateInfo*, const VkAllocationCallbacks*, VkFence* fence) {
    *fence = newHandle<VkFence>();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL beginCommandBuffer(VkCommandBuffer, const VkCommandBufferBeginInfo*) {
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL endCommandBuffer(VkCommandBuffer) {
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL queueSubmit(VkQueue, std::uint32_t, const VkSubmitInfo*, VkFence) {
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL waitForFences(VkDevice, std::uint32_t, const VkFence*, VkBool32, std::uint64_t) {
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL getFenceStatus(VkDevice, VkFence) {
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice, const VkBufferCreateInfo*, const VkAllocationCallbacks*, VkBuffer* buffer) {
    *buffer = newHandle<VkBuffer>();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL bufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements* requirements) {
    *requirements = {65536, 256, 1};
}

VKAPI_ATTR VkResult VKAPI_CALL bindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) {
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice, VkBuffer, const VkAllocationCallbacks*) {}

struct RecordedCopy {
    bool toBuffer;
    VkImage image;
    VkBuffer buffer;
    VkImageAspectFlags aspect;
    VkExtent3D extent;
};

std::vector<RecordedCopy>& copies() {
    static std::vector<RecordedCopy> recorded;
    return recorded;
}

VKAPI_ATTR void VKAPI_CALL cmdCopyImageToBuffer(VkCommandBuffer, VkImage image, VkImageLayout, VkBuffer buffer, std::uint32_t count, const VkBufferImageCopy* regions) {
    Require(count == 1, "a depth plane copy must be one region");
    copies().push_back({true, image, buffer, regions->imageSubresource.aspectMask, regions->imageExtent});
}

VKAPI_ATTR void VKAPI_CALL cmdCopyBufferToImage(VkCommandBuffer, VkBuffer buffer, VkImage image, VkImageLayout, std::uint32_t count, const VkBufferImageCopy* regions) {
    Require(count == 1, "a depth plane copy must be one region");
    copies().push_back({false, image, buffer, regions->imageSubresource.aspectMask, regions->imageExtent});
}

VKAPI_ATTR void VKAPI_CALL freeCommandBuffers(VkDevice, VkCommandPool, std::uint32_t, const VkCommandBuffer*) {}
VKAPI_ATTR void VKAPI_CALL destroyFence(VkDevice, VkFence, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL destroyImageView(VkDevice, VkImageView, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL destroyImage(VkDevice, VkImage, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL cmdPipelineBarrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, std::uint32_t, const VkMemoryBarrier*, std::uint32_t, const VkBufferMemoryBarrier*, std::uint32_t, const VkImageMemoryBarrier*) {}
VKAPI_ATTR void VKAPI_CALL cmdClearDepthStencilImage(VkCommandBuffer, VkImage, VkImageLayout, const VkClearDepthStencilValue*, std::uint32_t, const VkImageSubresourceRange*) {}

VKAPI_ATTR void VKAPI_CALL formatProperties(VkPhysicalDevice, VkFormat, VkFormatProperties* properties) {
    *properties = {};
    properties->optimalTilingFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
}

PFN_vkVoidFunction VKAPI_CALL deviceProc(VkDevice, const char* name) {
    static const std::map<std::string_view, PFN_vkVoidFunction> table{
        {"vkCreateImage", reinterpret_cast<PFN_vkVoidFunction>(createImage)},
        {"vkGetImageMemoryRequirements", reinterpret_cast<PFN_vkVoidFunction>(imageMemoryRequirements)},
        {"vkAllocateMemory", reinterpret_cast<PFN_vkVoidFunction>(allocateMemory)},
        {"vkBindImageMemory", reinterpret_cast<PFN_vkVoidFunction>(bindImageMemory)},
        {"vkCreateImageView", reinterpret_cast<PFN_vkVoidFunction>(createImageView)},
        {"vkAllocateCommandBuffers", reinterpret_cast<PFN_vkVoidFunction>(allocateCommandBuffers)},
        {"vkCreateFence", reinterpret_cast<PFN_vkVoidFunction>(createFence)},
        {"vkBeginCommandBuffer", reinterpret_cast<PFN_vkVoidFunction>(beginCommandBuffer)},
        {"vkEndCommandBuffer", reinterpret_cast<PFN_vkVoidFunction>(endCommandBuffer)},
        {"vkQueueSubmit", reinterpret_cast<PFN_vkVoidFunction>(queueSubmit)},
        {"vkWaitForFences", reinterpret_cast<PFN_vkVoidFunction>(waitForFences)},
        {"vkGetFenceStatus", reinterpret_cast<PFN_vkVoidFunction>(getFenceStatus)},
        {"vkFreeCommandBuffers", reinterpret_cast<PFN_vkVoidFunction>(freeCommandBuffers)},
        {"vkDestroyFence", reinterpret_cast<PFN_vkVoidFunction>(destroyFence)},
        {"vkDestroyImageView", reinterpret_cast<PFN_vkVoidFunction>(destroyImageView)},
        {"vkDestroyImage", reinterpret_cast<PFN_vkVoidFunction>(destroyImage)},
        {"vkFreeMemory", reinterpret_cast<PFN_vkVoidFunction>(freeMemory)},
        {"vkCmdPipelineBarrier", reinterpret_cast<PFN_vkVoidFunction>(cmdPipelineBarrier)},
        {"vkCmdClearDepthStencilImage", reinterpret_cast<PFN_vkVoidFunction>(cmdClearDepthStencilImage)},
        {"vkCreateBuffer", reinterpret_cast<PFN_vkVoidFunction>(createBuffer)},
        {"vkGetBufferMemoryRequirements", reinterpret_cast<PFN_vkVoidFunction>(bufferMemoryRequirements)},
        {"vkBindBufferMemory", reinterpret_cast<PFN_vkVoidFunction>(bindBufferMemory)},
        {"vkDestroyBuffer", reinterpret_cast<PFN_vkVoidFunction>(destroyBuffer)},
        {"vkCmdCopyImageToBuffer", reinterpret_cast<PFN_vkVoidFunction>(cmdCopyImageToBuffer)},
        {"vkCmdCopyBufferToImage", reinterpret_cast<PFN_vkVoidFunction>(cmdCopyBufferToImage)}
    };
    const auto it = table.find(name);
    return it == table.end() ? nullptr : it->second;
}

struct View {
    std::uint64_t address;
    std::uint32_t format;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t tileMode = TileDepth64KB;
    std::uint32_t type = Type2D;
    std::uint32_t lastArray = 0;
};

std::array<std::uint32_t, 8> tsharp(const View& view) {
    std::array<std::uint32_t, 8> words{};
    const auto base = view.address >> 8u;
    const auto widthMinus1 = view.width - 1u;
    const auto heightMinus1 = view.height - 1u;
    words[0] = static_cast<std::uint32_t>(base);
    words[1] = static_cast<std::uint32_t>((base >> 32u) & 0xffu) | ((view.format & 0x1ffu) << 20u) | ((widthMinus1 & 0x3u) << 30u);
    words[2] = ((widthMinus1 >> 2u) & 0xfffu) | ((heightMinus1 & 0x3fffu) << 14u);
    words[3] = 4u | (5u << 3u) | (6u << 6u) | (7u << 9u) | ((view.tileMode & 0x1fu) << 20u) | ((view.type & 0xfu) << 28u);
    words[4] = view.lastArray & 0x1fffu;
    return words;
}

std::shared_ptr<Texture> lookup(const Context& context, const View& view) {
    const auto words = tsharp(view);
    return DepthSurfaceTexture(context, words, DecodeTextureResource(words), VkComponentMapping{});
}

std::optional<DepthPlane> storagePlane(const Context& context, const View& view, std::uint32_t mip = 0) {
    const auto words = tsharp(view);
    return DepthPlaneForStorage(context, DecodeTextureResource(words), mip);
}

bool storageRejected(const Context& context, const View& view, std::uint32_t mip = 0) {
    try {
        static_cast<void>(storagePlane(context, view, mip));
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

void StoragePlaneTests(const Context& context) {
    constexpr std::uint64_t depth = 0x60000000;
    constexpr std::uint64_t stencil = 0x61000000;
    DepthSurfaceView(context, {depth, stencil, {128, 64}, VK_FORMAT_D32_SFLOAT_S8_UINT, 1.0f, 0});
    Require(!storagePlane(context, {0x62000000, FormatR8Uint, 128, 64}).has_value(), "memory without a depth surface must stay a plain storage image");
    const auto stencilPlane = storagePlane(context, {stencil, FormatR8Uint, 128, 64});
    Require(stencilPlane.has_value() && stencilPlane->aspect == VK_IMAGE_ASPECT_STENCIL_BIT && stencilPlane->extent.width == 128 && stencilPlane->extent.height == 64 && stencilPlane->staging != VK_NULL_HANDLE, "an R8 storage view of the stencil plane must bind the stencil aspect");
    const auto depthPlane = storagePlane(context, {depth, FormatR32Float, 128, 64});
    Require(depthPlane.has_value() && depthPlane->aspect == VK_IMAGE_ASPECT_DEPTH_BIT && depthPlane->image == stencilPlane->image && depthPlane->staging == stencilPlane->staging, "an R32 storage view of the depth plane must bind the depth aspect of the same image");
    Require(storageRejected(context, {stencil, FormatR16Unorm, 128, 64}), "a 16-bit storage view of the stencil plane must be refused");
    Require(storageRejected(context, {depth, FormatR8Uint, 128, 64}), "an 8-bit storage view of a 32-bit depth plane must be refused");
    Require(storageRejected(context, {stencil, FormatR8Uint, 64, 64}), "a storage view of another extent must be refused");
    Require(storageRejected(context, {stencil, FormatR8Uint, 128, 64}, 1), "a storage view of another level must be refused");
    const auto arrayPlane = storagePlane(context, {stencil, FormatR8Uint, 128, 64, TileDepth64KB, Type2DArray, 0});
    Require(arrayPlane.has_value() && arrayPlane->aspect == VK_IMAGE_ASPECT_STENCIL_BIT, "a one-slice 2D array storage view of the stencil plane must bind the stencil aspect");
    Require(storageRejected(context, {stencil, FormatR8Uint, 128, 64, TileDepth64KB, Type2DArray, 1}), "a two-slice 2D array storage view must be refused");

    const auto storage = newHandle<VkImage>();
    copies().clear();
    RecordDepthPlaneLoad(context, VK_NULL_HANDLE, *stencilPlane, storage);
    Require(copies().size() == 2, "a plane load must copy twice");
    Require(copies()[0].toBuffer && copies()[0].image == stencilPlane->image && copies()[0].aspect == VK_IMAGE_ASPECT_STENCIL_BIT && copies()[0].buffer == stencilPlane->staging, "a plane load must first read the plane into the staging buffer");
    Require(!copies()[1].toBuffer && copies()[1].image == storage && copies()[1].aspect == VK_IMAGE_ASPECT_COLOR_BIT && copies()[1].extent.width == 128 && copies()[1].extent.height == 64, "a plane load must then fill the storage image");
    copies().clear();
    RecordDepthPlaneStore(context, VK_NULL_HANDLE, *stencilPlane, storage);
    Require(copies().size() == 2, "a plane store must copy twice");
    Require(copies()[0].toBuffer && copies()[0].image == storage && copies()[0].aspect == VK_IMAGE_ASPECT_COLOR_BIT, "a plane store must first read the storage image");
    Require(!copies()[1].toBuffer && copies()[1].image == stencilPlane->image && copies()[1].aspect == VK_IMAGE_ASPECT_STENCIL_BIT, "a plane store must then write the stencil aspect of the depth image");
}

}

void RunDepthSurfaceReuseTests() {
    static int deviceTag = 0;
    Context context{};
    context.device = reinterpret_cast<VkDevice>(&deviceTag);
    context.deviceProc = deviceProc;
    context.formatProperties = formatProperties;
    context.memory.memoryTypeCount = 1;
    context.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    context.limits.maxFramebufferWidth = 16384;
    context.limits.maxFramebufferHeight = 16384;
    constexpr std::uint64_t depth32 = 0x40000000;
    constexpr std::uint64_t depth16 = 0x50000000;
    DepthSurfaceView(context, {depth32, 0, {384, 384}, VK_FORMAT_D32_SFLOAT, 1.0f, 0});
    DepthSurfaceView(context, {depth16, 0, {256, 256}, VK_FORMAT_D16_UNORM, 1.0f, 0});

    Require(lookup(context, {depth32, FormatR32Float, 384, 384}) != nullptr, "an R32F view of a D32 surface's own extent must sample its depth plane");
    Require(lookup(context, {depth16, FormatR16Unorm, 256, 256}) != nullptr, "an R16 view of a D16 surface's own extent must sample its depth plane");
    Require(lookup(context, {depth32, FormatR32Uint, 384, 384}) != nullptr, "a 32-bit raw depth bits view of a D32 surface must sample its depth plane");
    Require(lookup(context, {depth16, FormatR16Uint, 256, 256}) != nullptr, "a 16-bit raw depth bits view of a D16 surface must sample its depth plane");

    const auto arrayTexture = lookup(context, {depth32, FormatR32Float, 384, 384, TileDepth64KB, Type2DArray, 0});
    Require(arrayTexture != nullptr && arrayTexture->SampledViewRange(false).type == VK_IMAGE_VIEW_TYPE_2D_ARRAY && arrayTexture->FirstLayerView() != VK_NULL_HANDLE && arrayTexture->SampledViewRange(true).type == VK_IMAGE_VIEW_TYPE_2D, "a one-slice 2D array view of a depth surface must sample its depth plane through array and first-layer views");
    Require(lookup(context, {depth32, FormatR32Float, 192, 192}) == nullptr, "a view of another extent over a depth surface must be read as reused memory");
    Require(lookup(context, {depth32, FormatR11G11B10Float, 384, 384}) == nullptr, "an R11G11B10 view of a D32 surface's own extent must be read as reused memory");
    Require(lookup(context, {depth32, FormatR32Float, 512, 512, TileDepth64KB, Type2DArray, 5}) == nullptr, "a 2D array view of another extent over a depth surface must be read as reused memory");
    Require(lookup(context, {depth32, FormatR32Uint, 384, 384, TileRenderTarget64KB}) == nullptr, "an R32 uint view without a depth layout must be read as reused memory");
    Require(lookup(context, {depth16, FormatR32Float, 256, 256}) == nullptr, "an R32F view of a D16 surface's own extent must be read as reused memory");
    StoragePlaneTests(context);

    ClearDepthSurfaces(context.device);
}
