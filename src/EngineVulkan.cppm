module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>
// vulkan_core must go before glfw
#include <GLFW/glfw3.h>

#include <cassert>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Block for my LSP
#if defined(__clang__)
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vulkan/vulkan_raii.hpp>
#endif

export module engine_vulkan;

export import :context;
export import :device;
export import :swapchain;
export import :pipeline;
export import :buffer;

#if !defined(__clang__)
import vulkan;
import std;
#endif

constexpr uint32_t WIDTH = 1920;
constexpr uint32_t HEIGHT = 1080;

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

const std::vector<Vertex> vertices = {{{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
                                      {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
                                      {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
                                      {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}};
const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

export class VulkanTutorial {
 public:
  void run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
  }

 private:
  void initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, frameBufferResizeCallback);
  }

  void initVulkan() {
    context.init(window);
    device.init(context);
    swapchain.init(context, device, window);
    pipeline.init(device, swapchain);
    createVertexBuffer();
    createIndexBuffer();
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
  }

  static void frameBufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto app = reinterpret_cast<VulkanTutorial*>(glfwGetWindowUserPointer(window));
    app->frameBufferResized = true;
  }

  void createVertexBuffer() {
    vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    VulkanBuffer stagingBuffer = VulkanBufferFactory::create(
        device, bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
    void* data = nullptr;
    vmaMapMemory(device.allocator, stagingBuffer.allocation, &data);
    std::memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vmaUnmapMemory(device.allocator, stagingBuffer.allocation);

    vertexBuffer = VulkanBufferFactory::create(
        device, bufferSize,
        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        VMA_MEMORY_USAGE_AUTO);

    copyBuffer(stagingBuffer.buffer, vertexBuffer.buffer, bufferSize);
  }

  void createIndexBuffer() {
    vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();
    VulkanBuffer stagingBuffer = VulkanBufferFactory::create(
        device, bufferSize, vk::BufferUsageFlagBits::eTransferSrc,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST);

    void* data = nullptr;
    vmaMapMemory(device.allocator, stagingBuffer.allocation, &data);
    std::memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    vmaUnmapMemory(device.allocator, stagingBuffer.allocation);

    indexBuffer = VulkanBufferFactory::create(
        device, bufferSize,
        vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        VMA_MEMORY_USAGE_AUTO);

    copyBuffer(stagingBuffer.buffer, indexBuffer.buffer, bufferSize);
  }

  void createUniformBuffers() {
    vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      VulkanBuffer buffer = VulkanBufferFactory::create(
          device, bufferSize, vk::BufferUsageFlagBits::eUniformBuffer,
          VMA_MEMORY_USAGE_AUTO);
      uniformBuffersMapped.emplace_back(buffer.allocInfo.pMappedData);
      uniformBuffers.emplace_back(std::move(buffer));
    }
  }

  void createDescriptorPool() {
    vk::DescriptorPoolSize poolSize{.type = vk::DescriptorType::eUniformBuffer,
                                    .descriptorCount = MAX_FRAMES_IN_FLIGHT};
    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize};
    descriptorPool = vk::raii::DescriptorPool(device.device, poolInfo);
  }

  void createDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
                                                 *pipeline.descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()};
    descriptorSets = device.device.allocateDescriptorSets(allocInfo);
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      vk::DescriptorBufferInfo bufferInfo{.buffer = uniformBuffers[i].buffer,
                                          .offset = 0,
                                          .range = sizeof(UniformBufferObject)};
      vk::WriteDescriptorSet descriptorWrite{
          .dstSet = *descriptorSets[i],
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo = &bufferInfo};
      device.device.updateDescriptorSets(descriptorWrite, {});
    }
  }

  void copyBuffer(vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size) {
    vk::CommandBufferAllocateInfo allocInfo{.commandPool = device.transferCommandPool,
                                            .level = vk::CommandBufferLevel::ePrimary,
                                            .commandBufferCount = 1};
    vk::raii::CommandBuffer commandCopyBuffer =
        std::move(device.device.allocateCommandBuffers(allocInfo).front());
    commandCopyBuffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, vk::BufferCopy(0, 0, size));
    commandCopyBuffer.end();
    device.transferQueue.submit(
        vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandCopyBuffer},
        nullptr);
    device.transferQueue.waitIdle();
  }

  uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties =
        device.physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
      if ((typeFilter & (1 << i)) &&
          (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
        return i;
      }
    }
    throw std::runtime_error("failed to find suitable memory type");
  }

  void createCommandBuffers() {
    commandBuffers.clear();
    vk::CommandBufferAllocateInfo allocInfo{.commandPool = device.graphicsCommandPool,
                                            .level = vk::CommandBufferLevel::ePrimary,
                                            .commandBufferCount = MAX_FRAMES_IN_FLIGHT};
    commandBuffers = vk::raii::CommandBuffers(device.device, allocInfo);
  }

  void recordCommandBuffer(uint32_t imageIndex) {
    auto& commandBuffer = commandBuffers[frameIndex];
    commandBuffer.begin({});
    transitionImageLayout(imageIndex, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eColorAttachmentOptimal, {},
                          vk::AccessFlagBits2::eColorAttachmentWrite,
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::RenderingAttachmentInfo attachmentInfo{
        .imageView = swapchain.swapChainImageViews[imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor};

    vk::RenderingInfo renderingInfo = {
        .renderArea = {.offset = {0, 0}, .extent = swapchain.swapChainExtent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachmentInfo};

    commandBuffer.beginRendering(renderingInfo);
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                               *pipeline.graphicsPipeline);
    vk::Viewport viewport{0.0f,
                          0.0f,
                          static_cast<float>(swapchain.swapChainExtent.width),
                          static_cast<float>(swapchain.swapChainExtent.height),
                          0.0f,
                          1.0f};
    commandBuffer.setViewport(0, viewport);

    vk::Rect2D scissor{vk::Offset2D{0, 0}, swapchain.swapChainExtent};
    commandBuffer.setScissor(0, scissor);

    commandBuffer.bindVertexBuffers(0, vertexBuffer.buffer, {0});
    commandBuffer.bindIndexBuffer(
        indexBuffer.buffer, 0, vk::IndexTypeValue<decltype(indices)::value_type>::value);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     pipeline.pipelineLayout, 0,
                                     *descriptorSets[frameIndex], nullptr);

    commandBuffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    commandBuffer.endRendering();

    transitionImageLayout(imageIndex, vk::ImageLayout::eColorAttachmentOptimal,
                          vk::ImageLayout::ePresentSrcKHR,
                          vk::AccessFlagBits2::eColorAttachmentWrite, {},
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                          vk::PipelineStageFlagBits2::eBottomOfPipe);
    commandBuffer.end();
  }

  void transitionImageLayout(uint32_t imageindex, vk::ImageLayout oldLayout,
                             vk::ImageLayout newLayout, vk::AccessFlags2 srcAccessMask,
                             vk::AccessFlags2 dstAccessMask,
                             vk::PipelineStageFlags2 srcStageMask,
                             vk::PipelineStageFlags2 dstStageMask) {
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchain.swapChainImages[imageindex],
        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};
    vk::DependencyInfo dependencyInfo = {.dependencyFlags = {},
                                         .imageMemoryBarrierCount = 1,
                                         .pImageMemoryBarriers = &barrier};

    commandBuffers[frameIndex].pipelineBarrier2(dependencyInfo);
  }

  void createSyncObjects() {
    assert(presentCompleteSemaphores.empty() && renderFinishedSemaphores.empty() &&
           inFlightFences.empty());
    for (size_t i = 0; i < swapchain.swapChainImages.size(); i++) {
      renderFinishedSemaphores.emplace_back(device.device, vk::SemaphoreCreateInfo());
    }

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
      presentCompleteSemaphores.emplace_back(device.device, vk::SemaphoreCreateInfo());
      inFlightFences.emplace_back(
          device.device,
          vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
    }
  }

  void mainLoop() {
    while (!glfwWindowShouldClose(window)) {
      glfwPollEvents();
      drawFrame();
    }

    device.device.waitIdle();
  }

  void drawFrame() {
    auto fenceResult =
        device.device.waitForFences(*inFlightFences[frameIndex], vk::True, UINT64_MAX);
    if (fenceResult != vk::Result::eSuccess) {
      throw std::runtime_error("failed to wait for fence");
    }
    auto imageIndex = acquireNextFrameImage();
    if (!imageIndex) return;
    updateUniformBuffer(frameIndex);

    device.device.resetFences(*inFlightFences[frameIndex]);

    commandBuffers[frameIndex].reset();
    recordCommandBuffer(*imageIndex);

    vk::PipelineStageFlags waitDestinationStageMask(
        vk::PipelineStageFlagBits::eColorAttachmentOutput);
    const vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*presentCompleteSemaphores[frameIndex],
        .pWaitDstStageMask = &waitDestinationStageMask,
        .commandBufferCount = 1,
        .pCommandBuffers = &*commandBuffers[frameIndex],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*renderFinishedSemaphores[*imageIndex]};

    device.graphicsQueue.submit(submitInfo, *inFlightFences[frameIndex]);

    vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*renderFinishedSemaphores[*imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &*swapchain.swapChain,
        .pImageIndices = &imageIndex.value()};

    presentGraphicsQueue(presentInfoKHR);

    frameIndex = (frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
  }

  std::optional<uint32_t> acquireNextFrameImage() {
    try {
      auto [result, idx] = swapchain.swapChain.acquireNextImage(
          UINT64_MAX, *presentCompleteSemaphores[frameIndex], nullptr);
      if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
      }
      return idx;
    } catch (vk::OutOfDateKHRError const&) {
      std::cout << "errorOutOfDateKHR acquiring next Image!\n";
      recreateSwapChain();
      return std::nullopt;
    }
  }

  void updateUniformBuffer(uint32_t currentImage) {
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime -
                                                                            startTime)
                     .count();

    UniformBufferObject ubo{};
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f),
                            glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.view = lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                      glm::vec3(0.0f, 0.0f, 1.0f));
    ubo.proj = glm::perspective(glm::radians(45.0f),
                                static_cast<float>(swapchain.swapChainExtent.width) /
                                    static_cast<float>(swapchain.swapChainExtent.height),
                                0.1f, 10.0f);
    ubo.proj[1][1] *= -1;
    std::memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
  }

  void presentGraphicsQueue(const vk::PresentInfoKHR& presentInfo) {
    try {
      auto result = device.graphicsQueue.presentKHR(presentInfo);
      if (result == vk::Result::eSuboptimalKHR || frameBufferResized) {
        frameBufferResized = false;
        recreateSwapChain();
        std::cout << "vk::Queue::presentKHR returned vk::Result::eSuboptimalKHR !\n";
      }

    } catch (vk::OutOfDateKHRError const&) {
      recreateSwapChain();
    }
  }

  void recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
      glfwGetFramebufferSize(window, &width, &height);
      glfwWaitEvents();
    }
    device.device.waitIdle();

    swapchain.recreate(context, device, window);

    frameIndex = 0;
  }

  void cleanup() {
    inFlightFences.clear();
    renderFinishedSemaphores.clear();
    presentCompleteSemaphores.clear();
    commandBuffers.clear();

    vertexBuffer = {};
    indexBuffer = {};

    uniformBuffers.clear();
    uniformBuffersMapped.clear();

    pipeline = {};
    swapchain.cleanup();
    descriptorSets.clear();
    descriptorPool = nullptr;
    vmaDestroyAllocator(device.allocator);
    device.allocator = nullptr;
    device = {};
    context = {};

    if (window) {
      glfwDestroyWindow(window);
    }
    glfwTerminate();
  }

  GLFWwindow* window = nullptr;

  VulkanContext context;
  VulkanDevice device;
  VulkanSwapchain swapchain;
  VulkanPipeline pipeline;
  VulkanBuffer vertexBuffer;
  VulkanBuffer indexBuffer;
  std::vector<VulkanBuffer> uniformBuffers;
  std::vector<void*> uniformBuffersMapped;
  vk::raii::DescriptorPool descriptorPool = nullptr;
  std::vector<vk::raii::DescriptorSet> descriptorSets;
  uint32_t frameIndex = 0;
  std::vector<vk::raii::CommandBuffer> commandBuffers;

  std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
  std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
  std::vector<vk::raii::Fence> inFlightFences;
  bool frameBufferResized = false;
};
