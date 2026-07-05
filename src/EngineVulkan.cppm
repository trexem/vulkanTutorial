module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>
// vulkan_core must go before glfw
#include <GLFW/glfw3.h>

#include <cassert>
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
export import :image;
export import :buffer;

#if !defined(__clang__)
import vulkan;
import std;
#endif

constexpr uint32_t WIDTH = 1920;
constexpr uint32_t HEIGHT = 1080;

constexpr int MAX_FRAMES_IN_FLIGHT = 2;

const std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},

    {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
    {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}};

const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4};

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
    createDepthResources();
    createTextureImage();
    createTextureImageView();
    createTextureSampler();
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

  void createDepthResources() {
    vk::Format depthFormat = device.depthFormat;
    depthImage = VulkanImageFactory::create(
        device, swapchain.swapChainExtent.width, swapchain.swapChainExtent.height,
        depthFormat, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment, VMA_MEMORY_USAGE_AUTO);
    depthImageView =
        createImageView(depthImage.image, depthFormat, vk::ImageAspectFlagBits::eDepth);
  }

  void createTextureImage() {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load("../data/textures/texture.jpg", &texWidth, &texHeight,
                                &texChannels, STBI_rgb_alpha);
    vk::DeviceSize imageSize = texWidth * texHeight * 4;
    if (!pixels) {
      throw std::runtime_error("failed to load texture image!");
    }

    VulkanBuffer stagingBuffer = VulkanBufferFactory::create(
        device, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
    void* data = nullptr;
    vmaMapMemory(device.allocator, stagingBuffer.allocation, &data);
    std::memcpy(data, pixels, static_cast<size_t>(imageSize));
    vmaUnmapMemory(device.allocator, stagingBuffer.allocation);

    stbi_image_free(pixels);

    textureImage = VulkanImageFactory::create(
        device, texWidth, texHeight, vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        VMA_MEMORY_USAGE_AUTO);

    vk::raii::CommandBuffer transferCommandBuffer =
        beginSingleTimeCommands(device.transferCommandPool);
    transitionImageLayout(
        transferCommandBuffer, textureImage.image, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal, {}, vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eTopOfPipe, vk::PipelineStageFlagBits2::eTransfer);
    copyBufferToImage(transferCommandBuffer, stagingBuffer.buffer, textureImage.image,
                      static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    endSingleTimeCommands(std::move(transferCommandBuffer), device.transferQueue);

    vk::raii::CommandBuffer graphicsCommandBuffer =
        beginSingleTimeCommands(device.graphicsCommandPool);
    transitionImageLayout(
        graphicsCommandBuffer, textureImage.image, vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eTransferWrite,
        vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eTransfer,
        vk::PipelineStageFlagBits2::eAllCommands);
    endSingleTimeCommands(std::move(graphicsCommandBuffer), device.graphicsQueue);
  }

  vk::raii::CommandBuffer beginSingleTimeCommands(
      const vk::raii::CommandPool& commandPool) {
    vk::CommandBufferAllocateInfo allocInfo{.commandPool = *commandPool,
                                            .level = vk::CommandBufferLevel::ePrimary,
                                            .commandBufferCount = 1};

    vk::raii::CommandBuffer commandBuffer =
        std::move(device.device.allocateCommandBuffers(allocInfo).front());

    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    commandBuffer.begin(beginInfo);

    return commandBuffer;
  }

  void endSingleTimeCommands(vk::raii::CommandBuffer&& commandBuffer,
                             const vk::raii::Queue& queue) {
    commandBuffer.end();
    vk::SubmitInfo submitInfo{.commandBufferCount = 1,
                              .pCommandBuffers = &*commandBuffer};

    queue.submit(submitInfo, nullptr);
    queue.waitIdle();
  }

  void copyBufferToImage(vk::raii::CommandBuffer& commandBuffer, const vk::Buffer& buffer,
                         vk::Image& image, uint32_t width, uint32_t height) {
    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                             .mipLevel = 0,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1}};
    commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal,
                                    region);
  }

  void createTextureImageView() {
    textureImageView = createImageView(textureImage.image, vk::Format::eR8G8B8A8Srgb,
                                       vk::ImageAspectFlagBits::eColor);
  }

  vk::raii::ImageView createImageView(vk::Image const& image, vk::Format format,
                                      vk::ImageAspectFlags aspectFlags) {
    vk::ImageViewCreateInfo viewInfo{.image = image,
                                     .viewType = vk::ImageViewType::e2D,
                                     .format = format,
                                     .subresourceRange = {.aspectMask = aspectFlags,
                                                          .baseMipLevel = 0,
                                                          .levelCount = 1,
                                                          .baseArrayLayer = 0,
                                                          .layerCount = 1}};
    return vk::raii::ImageView(device.device, viewInfo);
  }

  void createTextureSampler() {
    vk::PhysicalDeviceProperties properties = device.physicalDevice.getProperties();
    vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .anisotropyEnable = vk::True,
        .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eAlways,
        .unnormalizedCoordinates = vk::False};
    textureSampler = vk::raii::Sampler(device.device, samplerInfo);
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
    std::array<vk::DescriptorPoolSize, 2> poolSize{
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eUniformBuffer,
                               .descriptorCount = MAX_FRAMES_IN_FLIGHT},
        vk::DescriptorPoolSize{.type = vk::DescriptorType::eCombinedImageSampler,
                               .descriptorCount = MAX_FRAMES_IN_FLIGHT}};
    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = static_cast<uint32_t>(poolSize.size()),
        .pPoolSizes = poolSize.data()};
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
      vk::DescriptorImageInfo imageInfo{
          .sampler = textureSampler,
          .imageView = textureImageView,
          .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal};
      std::array<vk::WriteDescriptorSet, 2> descriptorWrites{
          vk::WriteDescriptorSet{.dstSet = *descriptorSets[i],
                                 .dstBinding = 0,
                                 .dstArrayElement = 0,
                                 .descriptorCount = 1,
                                 .descriptorType = vk::DescriptorType::eUniformBuffer,
                                 .pBufferInfo = &bufferInfo},
          vk::WriteDescriptorSet{
              .dstSet = *descriptorSets[i],
              .dstBinding = 1,
              .dstArrayElement = 0,
              .descriptorCount = 1,
              .descriptorType = vk::DescriptorType::eCombinedImageSampler,
              .pImageInfo = &imageInfo}};
      device.device.updateDescriptorSets(descriptorWrites, {});
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
    transitionImageLayout(commandBuffer, swapchain.swapChainImages[imageIndex],
                          vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eColorAttachmentOptimal, {},
                          vk::AccessFlagBits2::eColorAttachmentWrite,
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    transitionImageLayout(commandBuffer, depthImage.image, vk::ImageLayout::eUndefined,
                          vk::ImageLayout::eDepthAttachmentOptimal,
                          vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                          vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                          vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                              vk::PipelineStageFlagBits2::eLateFragmentTests,
                          vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                              vk::PipelineStageFlagBits2::eLateFragmentTests,
                          vk::ImageAspectFlagBits::eDepth);
    vk::ClearValue clearColor = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
    vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
    vk::RenderingAttachmentInfo colorAttachmentInfo{
        .imageView = swapchain.swapChainImageViews[imageIndex],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearColor};
    vk::RenderingAttachmentInfo depthAttachmentInfo{
        .imageView = depthImageView,
        .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearDepth};

    vk::RenderingInfo renderingInfo = {
        .renderArea = {.offset = {0, 0}, .extent = swapchain.swapChainExtent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo};

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

    transitionImageLayout(commandBuffer, swapchain.swapChainImages[imageIndex],
                          vk::ImageLayout::eColorAttachmentOptimal,
                          vk::ImageLayout::ePresentSrcKHR,
                          vk::AccessFlagBits2::eColorAttachmentWrite, {},
                          vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                          vk::PipelineStageFlagBits2::eBottomOfPipe);
    commandBuffer.end();
  }

  void transitionImageLayout(
      const vk::raii::CommandBuffer& commandBuffer, vk::Image image,
      vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
      vk::AccessFlags2 srcAccessMask, vk::AccessFlags2 dstAccessMask,
      vk::PipelineStageFlags2 srcStageMask, vk::PipelineStageFlags2 dstStageMask,
      vk::ImageAspectFlags imageAspectFlags = vk::ImageAspectFlagBits::eColor) {
    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {.aspectMask = imageAspectFlags,
                             .baseMipLevel = 0,
                             .levelCount = 1,
                             .baseArrayLayer = 0,
                             .layerCount = 1}};
    vk::DependencyInfo dependencyInfo = {.dependencyFlags = {},
                                         .imageMemoryBarrierCount = 1,
                                         .pImageMemoryBarriers = &barrier};

    commandBuffer.pipelineBarrier2(dependencyInfo);
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

    textureImage = {};
    depthImage = {};
    textureImageView = nullptr;
    depthImageView = nullptr;
    textureSampler = nullptr;
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
  VulkanImage textureImage;
  VulkanImage depthImage;
  vk::raii::ImageView textureImageView = nullptr;
  vk::raii::ImageView depthImageView = nullptr;
  vk::raii::Sampler textureSampler = nullptr;
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
