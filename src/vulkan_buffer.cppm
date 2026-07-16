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
    VulkanBuffer outBuffer;
    outBuffer.allocator = device.allocator;

    std::vector<uint32_t> uniqueFamilies;
    uniqueFamilies.push_back(device.graphicsQueueIndex);

    if ((usage & vk::BufferUsageFlagBits::eTransferDst) ||
        (usage & vk::BufferUsageFlagBits::eTransferSrc)) {
      uniqueFamilies.push_back(device.transferQueueIndex);
    }
    if (usage & vk::BufferUsageFlagBits::eStorageBuffer) {
      uniqueFamilies.push_back(device.computeQueueIndex);
    }
    std::sort(uniqueFamilies.begin(), uniqueFamilies.end());
    auto it = std::unique(uniqueFamilies.begin(), uniqueFamilies.end());
    uniqueFamilies.erase(it, uniqueFamilies.end());

    vk::BufferCreateInfo bufferInfo{.size = size, .usage = usage};
    if (uniqueFamilies.size() > 1) {
      bufferInfo.sharingMode = vk::SharingMode::eConcurrent;
      bufferInfo.queueFamilyIndexCount = static_cast<uint32_t>(uniqueFamilies.size());
      bufferInfo.pQueueFamilyIndices = uniqueFamilies.data();
    } else {
      bufferInfo.sharingMode = vk::SharingMode::eExclusive;
    }

    VmaAllocationCreateInfo allocCreateInfo{.usage = vmaUsage};
    if (vmaUsage == VMA_MEMORY_USAGE_AUTO_PREFER_HOST ||
        vmaUsage == VMA_MEMORY_USAGE_AUTO) {
      allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
      if (usage & vk::BufferUsageFlagBits::eTransferSrc) {
        // Staging Buffer
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
      } else if (usage & vk::BufferUsageFlagBits::eUniformBuffer) {
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                VMA_ALLOCATION_CREATE_MAPPED_BIT;
      } else {
        // Vertex or Index buffer
        allocCreateInfo.preferredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      }
    }
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
