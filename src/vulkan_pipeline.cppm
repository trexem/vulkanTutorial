module;

// Block for my LSP
#if defined(__clang__)
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vulkan/vulkan_raii.hpp>
#endif

export module engine_vulkan:pipeline;

export import :device;
export import :swapchain;

#if !defined(__clang__)
import vulkan;
import std;
#endif

export struct VulkanPipeline {
  vk::raii::PipelineLayout pipelineLayout = nullptr;
  vk::raii::Pipeline graphicsPipeline = nullptr;

  void init(const VulkanDevice& device, const VulkanSwapchain& swapchain) {
    createGraphicsPipeline(device, swapchain);
  }

 private:
  void createGraphicsPipeline(const VulkanDevice& device,
                              const VulkanSwapchain& swapchain) {
    // Shader stages
    vk::raii::ShaderModule shaderModule =
        createShaderModule(device, readFile("shaders/shader_slang.spv"));

    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = shaderModule,
        .pName = "vertMain",
        .pSpecializationInfo = nullptr};
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = shaderModule,
        .pName = "fragMain",
        .pSpecializationInfo = nullptr};

    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
                                                        fragShaderStageInfo};

    // Fixed functions
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo;

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        .topology = vk::PrimitiveTopology::eTriangleList};

    std::vector<vk::DynamicState> dynamicStates = {vk::DynamicState::eViewport,
                                                   vk::DynamicState::eScissor};

    vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()};

    vk::PipelineViewportStateCreateInfo viewPortState{.viewportCount = 1,
                                                      .scissorCount = 1};

    vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.0f};

    vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False};

    vk::PipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = vk::True,
        .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};
    vk::PipelineColorBlendStateCreateInfo colorBlending{
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment};

    // Pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{.setLayoutCount = 0,
                                                    .pushConstantRangeCount = 0};
    pipelineLayout = vk::raii::PipelineLayout(device.device, pipelineLayoutInfo);

    // Render passes for dynamic rendering
    vk::StructureChain<vk::GraphicsPipelineCreateInfo, vk::PipelineRenderingCreateInfo>
        pipelineCreateInfoChain = {
            {.stageCount = 2,
             .pStages = shaderStages,
             .pVertexInputState = &vertexInputInfo,
             .pInputAssemblyState = &inputAssembly,
             .pViewportState = &viewPortState,
             .pRasterizationState = &rasterizer,
             .pMultisampleState = &multisampling,
             .pColorBlendState = &colorBlending,
             .pDynamicState = &dynamicState,
             .layout = pipelineLayout,
             .renderPass = nullptr},
            {.colorAttachmentCount = 1,
             .pColorAttachmentFormats = &swapchain.swapChainSurfaceFormat.format}};

    graphicsPipeline =
        vk::raii::Pipeline(device.device, nullptr,
                           pipelineCreateInfoChain.get<vk::GraphicsPipelineCreateInfo>());
  }

  [[nodiscard]] vk::raii::ShaderModule createShaderModule(
      const VulkanDevice& device, const std::vector<char>& code) const {
    vk::ShaderModuleCreateInfo createInfo{
        .codeSize = code.size() * sizeof(char),
        .pCode = reinterpret_cast<const std::uint32_t*>(code.data())};

    vk::raii::ShaderModule shaderModule{device.device, createInfo};
    return shaderModule;
  }

  static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
      throw std::runtime_error("failed to open file!");
    }

    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();

    return buffer;
  }
};
