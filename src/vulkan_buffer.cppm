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

export module engine_vulkan:buffer;

export import :context;
export import :device;

#if !defined(__clang__)
import vulkan;
import std;
#endif

export struct VulkanBuffer {
  VmaAllocator allocator = nullptr;
  vk::Buffer buffer = nullptr;
  VmaAllocation allocation = nullptr;
  VmaAllocationInfo allocInfo{};

  ~VulkanBuffer() {
    if (allocator && buffer && allocation) {
      vmaDestroyBuffer(allocator, buffer, allocation);
    }
  }

  VulkanBuffer() = default;
  VulkanBuffer(VulkanBuffer&& other) noexcept { *this = std::move(other); }
  VulkanBuffer& operator=(VulkanBuffer&& other) noexcept {
    if (this != &other) {
      if (allocator && buffer && allocation)
        vmaDestroyBuffer(allocator, VkBuffer(buffer), allocation);
      allocator = other.allocator;
      buffer = other.buffer;
      allocation = other.allocation;
      allocInfo = other.allocInfo;
      other.buffer = nullptr;
      other.allocation = nullptr;
    }
    return *this;
  }
};

export class VulkanBufferFactory {
 public:
  static VulkanBuffer create(const VulkanDevice& device, vk::DeviceSize size,
                             vk::BufferUsageFlags usage, VmaMemoryUsage vmaUsage) {
    std::array<uint32_t, 2> queueFamilyIndices = {device.graphicsQueueIndex,
                                                  device.transferQueueIndex};
    VulkanBuffer outBuffer;
    outBuffer.allocator = device.allocator;

    vk::BufferCreateInfo bufferInfo{.size = size,
                                    .usage = usage,
                                    .sharingMode = vk::SharingMode::eConcurrent,
                                    .queueFamilyIndexCount = 2,
                                    .pQueueFamilyIndices = queueFamilyIndices.data()};
    VmaAllocationCreateInfo allocCreateInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = vmaUsage,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT};
    VkBuffer rawBuffer;
    if (vmaCreateBuffer(device.allocator,
                        reinterpret_cast<const VkBufferCreateInfo*>(&bufferInfo),
                        &allocCreateInfo, &rawBuffer, &outBuffer.allocation,
                        &outBuffer.allocInfo) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate buffer via VMA!");
    }

    outBuffer.buffer = vk::Buffer(rawBuffer);
    return outBuffer;
  }
};
