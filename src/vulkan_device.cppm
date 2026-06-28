module;

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
  vk::raii::CommandPool commandPool = nullptr;
  uint32_t queueIndex = ~0;

  void init(const VulkanContext& context) {
    pickPhysicalDevice(context);
    createLogicalDevice(context);
    createCommandPool();
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
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    bool supportsRequiredFeatures =
        features.template get<vk::PhysicalDeviceVulkan11Features>()
            .shaderDrawParameters &&
        features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
            .extendedDynamicState;

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

  void createLogicalDevice(const VulkanContext& context) {
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties =
        physicalDevice.getQueueFamilyProperties();

    for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++) {
      if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
          physicalDevice.getSurfaceSupportKHR(qfpIndex, *context.surface)) {
        queueIndex = qfpIndex;
        break;
      }
    }
    if (queueIndex == ~0) {
      throw std::runtime_error(
          "Could not find a queue for graphics and present -> terminating");
    }
    float queuePriority = 0.5f;

    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
        featureChain = {
            {},                              // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true},  // vk::PhysicalDeviceVulkan11Features
            {
                .synchronization2 = true,
                .dynamicRendering = true,
            },  // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState =
                 true}  // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        };

    vk::DeviceQueueCreateInfo deviceQueueCreateInfo{.queueFamilyIndex = queueIndex,
                                                    .queueCount = 1,
                                                    .pQueuePriorities = &queuePriority};

    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data()};

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    graphicsQueue = vk::raii::Queue(device, queueIndex, 0);
  }

  void createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = queueIndex};
    commandPool = vk::raii::CommandPool(device, poolInfo);
  }
};
