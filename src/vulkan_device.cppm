module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>
// vulkan_core must go before glfw
#include <GLFW/glfw3.h>

#include <cassert>

// Block for my LSP
#if defined(__clang__)
#include <algorithm>
#include <cstdint>
#include <vulkan/vulkan_raii.hpp>
#endif

export module engine_vulkan:device;

import :context;

#if !defined(__clang__)
import vulkan;
import std;
#endif

export struct VulkanDevice {
  vk::raii::PhysicalDevice physicalDevice = nullptr;
  vk::raii::Device device = nullptr;
  vk::raii::Queue graphicsQueue = nullptr;
  vk::raii::Queue transferQueue = nullptr;
  vk::raii::Queue computeQueue = nullptr;
  vk::raii::CommandPool graphicsCommandPool = nullptr;
  vk::raii::CommandPool transferCommandPool = nullptr;
  vk::raii::CommandPool computeCommandPool = nullptr;
  vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;
  vk::Format depthFormat = vk::Format::eUndefined;
  uint32_t graphicsQueueIndex = ~0;
  uint32_t transferQueueIndex = ~0;
  uint32_t computeQueueIndex = ~0;
  VmaAllocator allocator = nullptr;

  void init(const VulkanContext& context) {
    pickPhysicalDevice(context);
    findQueueIndexes(context);
    createLogicalDevice();
    createCommandPools();
    initVma(context);
    depthFormat = findDepthFormat();
  }

 private:
  std::vector<const char*> requiredDeviceExtension = {vk::KHRSwapchainExtensionName};

  void pickPhysicalDevice(const VulkanContext& context) {
    std::vector<vk::raii::PhysicalDevice> physicalDevices =
        context.instance.enumeratePhysicalDevices();
    auto const devIt =
        std::ranges::find_if(physicalDevices, [&](auto const& physicalDevice) {
          return isDeviceSuitable(physicalDevice, context);
        });
    if (devIt == physicalDevices.end()) {
      throw std::runtime_error("failed to find a suitable GPU!");
    }
    physicalDevice = *devIt;
    msaaSamples = getMxUsableSampleCount();
  }

  bool isDeviceSuitable(vk::raii::PhysicalDevice const& physicalDevice,
                        const VulkanContext& context) {
    auto deviceProperties = physicalDevice.getProperties();
    auto deviceFeatures = physicalDevice.getFeatures();
    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    std::vector<const char*> requiredDeviceExtensions = {vk::KHRSwapchainExtensionName};
    auto availableDeviceExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    auto features = physicalDevice.template getFeatures2<
        vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
        vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>();
    bool supportsRequiredFeatures =
        features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
        features.template get<vk::PhysicalDeviceVulkan11Features>()
            .shaderDrawParameters &&
        features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
            .extendedDynamicState &&
        features.template get<vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>()
            .timelineSemaphore;

    bool supportsVulkan1_4 = deviceProperties.apiVersion >= vk::ApiVersion14;
    bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const& qfp) {
      return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
    });
    bool supportsAllRequiredExtensions = std::ranges::all_of(
        requiredDeviceExtensions,
        [&availableDeviceExtensions](auto const& requiredDeviceExtension) {
          return std::ranges::any_of(
              availableDeviceExtensions,
              [requiredDeviceExtension](auto const& availableDeviceExtension) {
                return strcmp(availableDeviceExtension.extensionName,
                              requiredDeviceExtension) == 0;
              });
        });
    return supportsVulkan1_4 && supportsGraphics && supportsAllRequiredExtensions &&
           supportsRequiredFeatures;
  }

  void findQueueIndexes(const VulkanContext& context) {
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
        physicalDevice.getQueueFamilyProperties();
    for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
      if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
          physicalDevice.getSurfaceSupportKHR(i, *context.surface)) {
        graphicsQueueIndex = i;
        break;
      }
    }

    if (graphicsQueueIndex == ~0) {
      throw std::runtime_error(
          "Could not find a queue family supporting graphics + Present!");
    }

    for (uint32_t i = 0; i < queueFamilyProperties.size(); i++) {
      const auto& flags = queueFamilyProperties[i].queueFlags;
      // Trying to get dedicated transfer queue family
      if ((flags & vk::QueueFlagBits::eTransfer) &&
          !(flags & vk::QueueFlagBits::eGraphics) &&
          !(flags & vk::QueueFlagBits::eCompute) && transferQueueIndex == ~0) {
        transferQueueIndex = i;
      }
      // Trying to get dedicated compute queue family
      if ((flags & vk::QueueFlagBits::eCompute) &&
          !(flags & vk::QueueFlagBits::eGraphics) && computeQueueIndex == ~0) {
        computeQueueIndex = i;
      }
    }
    // If no dedicated transfer queue, look for any queue supporting transfer that isn't
    // graphics
    if (transferQueueIndex == ~0) {
      for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
        if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eTransfer) &&
            i != graphicsQueueIndex) {
          transferQueueIndex = i;
          break;
        }
      }
    }
    // Ultimate transfer fallback: just share the graphics queue family
    if (transferQueueIndex == ~0) {
      if (queueFamilyProperties[graphicsQueueIndex].queueFlags &
          vk::QueueFlagBits::eTransfer) {
        transferQueueIndex = graphicsQueueIndex;
      }
    }

    // If no dedicated compute queue, look for any queue supporting compute that isn't
    // graphics
    if (computeQueueIndex == ~0) {
      for (uint32_t i = 0; i < queueFamilyProperties.size(); ++i) {
        if ((queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eCompute) &&
            i != graphicsQueueIndex) {
          computeQueueIndex = i;
          break;
        }
      }
    }
    // Ultimate compute fallback: just share the graphics queue family
    if (computeQueueIndex == ~0) {
      if (queueFamilyProperties[graphicsQueueIndex].queueFlags &
          vk::QueueFlagBits::eCompute) {
        computeQueueIndex = graphicsQueueIndex;
      }
    }

    // Final safety validation
    if (transferQueueIndex == ~0 || computeQueueIndex == ~0) {
      throw std::runtime_error(
          "Incompatible hardware: Could not resolve fallback Compute/Transfer "
          "requirements.");
    }
    std::cout << "Graphics, Transfer, Compute " << graphicsQueueIndex << " "
              << transferQueueIndex << " " << computeQueueIndex << std::endl;
  }

  void createLogicalDevice() {
    float queuePriority = 0.5f;

    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                       vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>
        featureChain = {
            {.features = {.samplerAnisotropy = true}},  // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true},  // vk::PhysicalDeviceVulkan11Features
            {
                .synchronization2 = true,
                .dynamicRendering = true,
            },  // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState =
                 true},  // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
            {.timelineSemaphore = true}  // vk::PhysicalDeviceTimelineSempahoreFeaturesKHR
        };

    std::vector<vk::DeviceQueueCreateInfo> deviceQueueCreateInfos{
        {.queueFamilyIndex = graphicsQueueIndex,
         .queueCount = 1,
         .pQueuePriorities = &queuePriority},
        {.queueFamilyIndex = transferQueueIndex,
         .queueCount = 1,
         .pQueuePriorities = &queuePriority},
        {.queueFamilyIndex = computeQueueIndex,
         .queueCount = 1,
         .pQueuePriorities = &queuePriority}};

    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 3,
        .pQueueCreateInfos = deviceQueueCreateInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data()};

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    graphicsQueue = vk::raii::Queue(device, graphicsQueueIndex, 0);
    transferQueue = vk::raii::Queue(device, transferQueueIndex, 0);
    computeQueue = vk::raii::Queue(device, computeQueueIndex, 0);
  }

  void createCommandPools() {
    vk::CommandPoolCreateInfo graphicsPoolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = graphicsQueueIndex};
    graphicsCommandPool = vk::raii::CommandPool(device, graphicsPoolInfo);
    vk::CommandPoolCreateInfo transferPoolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                 vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = transferQueueIndex};
    transferCommandPool = vk::raii::CommandPool(device, transferPoolInfo);
    vk::CommandPoolCreateInfo computePoolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = computeQueueIndex};
    computeCommandPool = vk::raii::CommandPool(device, computePoolInfo);
  }

  void initVma(const VulkanContext& context) {
    VmaVulkanFunctions vulkanFunctions{
        .vkGetInstanceProcAddr = context.instance.getDispatcher()->vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = device.getDispatcher()->vkGetDeviceProcAddr};

    VmaAllocatorCreateInfo allocatorCreateInfo{.physicalDevice = *physicalDevice,
                                               .device = *device,
                                               .pVulkanFunctions = &vulkanFunctions,
                                               .instance = *context.instance,
                                               .vulkanApiVersion = VK_API_VERSION_1_4};

    vmaCreateAllocator(&allocatorCreateInfo, &allocator);
  }

  vk::Format findSupportedFormat(const std::vector<vk::Format>& candidates,
                                 vk::ImageTiling tiling,
                                 vk::FormatFeatureFlags features) {
    for (const auto format : candidates) {
      vk::FormatProperties props = physicalDevice.getFormatProperties(format);
      if ((tiling == vk::ImageTiling::eLinear &&
           (props.linearTilingFeatures & features) == features) ||
          tiling == vk::ImageTiling::eOptimal &&
              (props.optimalTilingFeatures & features) == features) {
        return format;
      }
    }
    throw std::runtime_error("failed to find supported format!");
  }

  vk::Format findDepthFormat() {
    return findSupportedFormat({vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
                                vk::Format::eD24UnormS8Uint},
                               vk::ImageTiling::eOptimal,
                               vk::FormatFeatureFlagBits::eDepthStencilAttachment);
  }

  vk::SampleCountFlagBits getMxUsableSampleCount() {
    vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();

    vk::SampleCountFlags counts = properties.limits.framebufferColorSampleCounts &
                                  properties.limits.framebufferDepthSampleCounts;
    if (counts & vk::SampleCountFlagBits::e64) {
      return vk::SampleCountFlagBits::e64;
    }
    if (counts & vk::SampleCountFlagBits::e32) {
      return vk::SampleCountFlagBits::e32;
    }
    if (counts & vk::SampleCountFlagBits::e16) {
      return vk::SampleCountFlagBits::e16;
    }
    if (counts & vk::SampleCountFlagBits::e8) {
      return vk::SampleCountFlagBits::e8;
    }
    if (counts & vk::SampleCountFlagBits::e4) {
      return vk::SampleCountFlagBits::e4;
    }
    if (counts & vk::SampleCountFlagBits::e2) {
      return vk::SampleCountFlagBits::e2;
    }
    return vk::SampleCountFlagBits::e1;
  }
};
