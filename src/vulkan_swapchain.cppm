module;

#include <vulkan/vulkan_core.h>
// vulkan_core must go before glfw
#include <GLFW/glfw3.h>

#include <cassert>

// Block for my LSP
#if defined(__clang__)
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vulkan/vulkan_raii.hpp>
#endif

export module engine_vulkan:swapchain;

import :context;
import :device;

#if !defined(__clang__)
import vulkan;
import std;
#endif

export struct VulkanSwapchain {
  vk::raii::SwapchainKHR swapChain = nullptr;
  std::vector<vk::Image> swapChainImages;
  vk::SurfaceFormatKHR swapChainSurfaceFormat;
  vk::Extent2D swapChainExtent;
  std::vector<vk::raii::ImageView> swapChainImageViews;

  void init(const VulkanContext& context, const VulkanDevice& device,
            GLFWwindow* window) {
    createSwapChain(context, device, window);
    createImageViews(device);
  }
  void recreate(const VulkanContext& context, const VulkanDevice& device,
                GLFWwindow* window) {
    cleanup();

    createSwapChain(context, device, window);
    createImageViews(device);
  }
  void cleanup() {
    swapChainImageViews.clear();
    swapChain = nullptr;
  }

 private:
  void createSwapChain(const VulkanContext& context, const VulkanDevice& device,
                       GLFWwindow* window) {
    vk::SurfaceCapabilitiesKHR surfaceCapabilities =
        device.physicalDevice.getSurfaceCapabilitiesKHR(*context.surface);
    swapChainExtent = chooseSwapExtent(surfaceCapabilities, window);
    uint32_t minImageCount = chooseSwapMinImageCount(surfaceCapabilities);

    std::vector<vk::SurfaceFormatKHR> availableFormats =
        device.physicalDevice.getSurfaceFormatsKHR(*context.surface);
    swapChainSurfaceFormat = chooseSwapSurfaceFormat(availableFormats);

    std::vector<vk::PresentModeKHR> availablePresentModes =
        device.physicalDevice.getSurfacePresentModesKHR(*context.surface);
    vk::PresentModeKHR presentMode = chooseSwapPresentMode(availablePresentModes);

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *context.surface,
        .minImageCount = minImageCount,
        .imageFormat = swapChainSurfaceFormat.format,
        .imageColorSpace = swapChainSurfaceFormat.colorSpace,
        .imageExtent = swapChainExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = true};

    swapChain = vk::raii::SwapchainKHR(device.device, swapChainCreateInfo);
    swapChainImages = swapChain.getImages();
  }

  vk::Extent2D chooseSwapExtent(vk::SurfaceCapabilitiesKHR const& capabilities,
                                GLFWwindow* window) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
      return capabilities.currentExtent;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    return {std::clamp<uint32_t>(width, capabilities.minImageExtent.width,
                                 capabilities.maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities.minImageExtent.height,
                                 capabilities.maxImageExtent.height)};
  }

  uint32_t chooseSwapMinImageCount(
      vk::SurfaceCapabilitiesKHR const& surfaceCapabilities) {
    auto minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if ((0 < surfaceCapabilities.maxImageCount) &&
        surfaceCapabilities.maxImageCount < minImageCount) {
      minImageCount = surfaceCapabilities.maxImageCount;
    }
    return minImageCount;
  }

  vk::SurfaceFormatKHR chooseSwapSurfaceFormat(
      std::vector<vk::SurfaceFormatKHR> const& availableFormats) {
    const auto formatIt = std::ranges::find_if(availableFormats, [](const auto& format) {
      return format.format == vk::Format::eR8G8B8A8Srgb &&
             format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
    });
    return formatIt != availableFormats.end() ? *formatIt : availableFormats[0];
  }

  vk::PresentModeKHR chooseSwapPresentMode(
      std::vector<vk::PresentModeKHR> const& availablePresentModes) {
    assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
      return presentMode == vk::PresentModeKHR::eFifo;
    }));
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value) {
                                 return vk::PresentModeKHR::eMailbox == value;
                               })
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
  }

  void createImageViews(const VulkanDevice& device) {
    assert(swapChainImageViews.empty());

    vk::ImageViewCreateInfo imageViewCreateInfo{
        .viewType = vk::ImageViewType::e2D,
        .format = swapChainSurfaceFormat.format,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};

    for (auto& image : swapChainImages) {
      imageViewCreateInfo.image = image;
      swapChainImageViews.emplace_back(device.device, imageViewCreateInfo);
    }
  }
};
