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
  vk::raii::CommandPool graphicsCommandPool = nullptr;
  vk::raii::CommandPool transferCommandPool = nullptr;
  uint32_t graphicsQueueIndex = ~0;
  uint32_t transferQueueIndex = ~0;
  VmaAllocator allocator = nullptr;

  void init(const VulkanContext& context) {
    pickPhysicalDevice(context);
    createLogicalDevice(context);
    createCommandPools();
    initVma(context);
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
        features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
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
      if (transferQueueIndex != ~0 && graphicsQueueIndex != ~0) break;
      if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eTransfer) &&
          !(queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics)) {
        transferQueueIndex = qfpIndex;
        continue;
      }
      if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
          physicalDevice.getSurfaceSupportKHR(qfpIndex, *context.surface)) {
        graphicsQueueIndex = qfpIndex;
        continue;
      }
    }

    if (graphicsQueueIndex == ~0) {
      throw std::runtime_error(
          "Could not find a graphics queue for graphics and present -> terminating");
    }
    if (transferQueueIndex == ~0) {
      throw std::runtime_error(
          "Could not find a transfer queue for different than graphics");
    }
    float queuePriority = 0.5f;

    vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
                       vk::PhysicalDeviceVulkan13Features,
                       vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>
        featureChain = {
            {.features = {.samplerAnisotropy = true}},  // vk::PhysicalDeviceFeatures2
            {.shaderDrawParameters = true},  // vk::PhysicalDeviceVulkan11Features
            {
                .synchronization2 = true,
                .dynamicRendering = true,
            },  // vk::PhysicalDeviceVulkan13Features
            {.extendedDynamicState =
                 true}  // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
        };

    std::vector<vk::DeviceQueueCreateInfo> deviceQueueCreateInfos{
        {.queueFamilyIndex = graphicsQueueIndex,
         .queueCount = 1,
         .pQueuePriorities = &queuePriority},
        {.queueFamilyIndex = transferQueueIndex,
         .queueCount = 1,
         .pQueuePriorities = &queuePriority}};

    vk::DeviceCreateInfo deviceCreateInfo{
        .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount = 2,
        .pQueueCreateInfos = deviceQueueCreateInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtension.size()),
        .ppEnabledExtensionNames = requiredDeviceExtension.data()};

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    graphicsQueue = vk::raii::Queue(device, graphicsQueueIndex, 0);
    transferQueue = vk::raii::Queue(device, transferQueueIndex, 0);
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
};
