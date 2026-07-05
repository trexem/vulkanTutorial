module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>
// vulkan_core must go before glfw
#include <GLFW/glfw3.h>

#include <cassert>

// Block for my LSP
#if defined(__clang__)
#include <cstdint>
#include <vulkan/vulkan_raii.hpp>
#endif

export module engine_vulkan:image;

export import :context;
export import :device;

#if !defined(__clang__)
import vulkan;
import std;
#endif

export struct VulkanImage {
  VmaAllocator allocator = nullptr;
  vk::Image image = nullptr;
  VmaAllocation allocation = nullptr;
  VmaAllocationInfo allocInfo{};

  ~VulkanImage() {
    if (allocator && image && allocation) {
      vmaDestroyImage(allocator, image, allocation);
    }
  }

  VulkanImage() = default;
  VulkanImage(VulkanImage&& other) noexcept { *this = std::move(other); }
  VulkanImage& operator=(VulkanImage&& other) noexcept {
    if (this != &other) {
      if (allocator && image && allocation) {
        vmaDestroyImage(allocator, image, allocation);
      }
      allocator = other.allocator;
      image = other.image;
      allocation = other.allocation;
      allocInfo = other.allocInfo;
      other.image = nullptr;
      other.allocation = nullptr;
    }
    return *this;
  }
};

export class VulkanImageFactory {
 public:
  static VulkanImage create(const VulkanDevice& device, uint32_t width, uint32_t height,
                            vk::Format format, vk::ImageUsageFlags usage,
                            VmaMemoryUsage vmaUsage) {
    VulkanImage outImage;
    outImage.allocator = device.allocator;

    vk::ImageCreateInfo imageInfo{.imageType = vk::ImageType::e2D,
                                  .format = format,
                                  .extent = {width, height, 1},
                                  .mipLevels = 1,
                                  .arrayLayers = 1,
                                  .samples = vk::SampleCountFlagBits::e1,
                                  .tiling = vk::ImageTiling::eOptimal,
                                  .usage = usage,
                                  .sharingMode = vk::SharingMode::eExclusive,
                                  .initialLayout = vk::ImageLayout::eUndefined};
    VmaAllocationCreateInfo allocCreateInfo{.usage = vmaUsage};
    if (vmaUsage == VMA_MEMORY_USAGE_AUTO) {
      allocCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    VkImage rawImage;
    if (vmaCreateImage(device.allocator,
                       reinterpret_cast<const VkImageCreateInfo*>(&imageInfo),
                       &allocCreateInfo, &rawImage, &outImage.allocation,
                       &outImage.allocInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate image via VMA!");
    }
    outImage.image = vk::Image(rawImage);
    return outImage;
  }
};
