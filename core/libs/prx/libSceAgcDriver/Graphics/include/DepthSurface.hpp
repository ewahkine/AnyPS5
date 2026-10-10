#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEPTHSURFACE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEPTHSURFACE_HPP

#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/State.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace AgcDriver::Graphics {

class Texture;

VkImageView DepthSurfaceView(const Context& context, const DepthTarget& target);
std::uint64_t DepthSliceBytes(VkExtent2D extent, std::uint32_t bytesPerTexel);
void ClearDepthSurfaces(VkDevice device);
bool DepthSurfaceAt(std::uint64_t address);
std::string DescribeDepthSurfaceAt(std::uint64_t address);

// One plane of a depth surface bound as a storage image: the storage image is a copy of the plane,
// loaded from it before the work and stored back into it after, so the depth image stays the
// surface's content for draws and samplers.
struct DepthPlane {
    VkImage image = VK_NULL_HANDLE;
    VkImageAspectFlags aspect = 0;
    VkExtent2D extent{};
    VkBuffer staging = VK_NULL_HANDLE;
};
// The plane a storage descriptor addresses, or nullopt when no depth surface lives at its address.
// Throws unless the descriptor views the whole plane as one 2D level of the plane's texel size.
std::optional<DepthPlane> DepthPlaneForStorage(const Context& context, const GuestTextureResource& resource, std::uint32_t mip);
void RecordDepthPlaneLoad(const Context& context, VkCommandBuffer commands, const DepthPlane& plane, VkImage storage);
void RecordDepthPlaneStore(const Context& context, VkCommandBuffer commands, const DepthPlane& plane, VkImage storage);
std::uint64_t HtileDepthClearAddress(std::span<const std::uint32_t> code, std::span<const std::uint32_t> userData, const std::array<std::uint32_t, 3>& numThreads);
void NoteHtileDepthClear(std::uint64_t htileAddress);
std::shared_ptr<Texture> DepthSurfaceTexture(const Context& context, std::span<const std::uint32_t> words, const GuestTextureResource& resource, VkComponentMapping components);

}

#endif
