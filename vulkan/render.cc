module vulkan.render;

import "vulkan_config.h";
import vulkan.tool;
import vulkan.buffer;
import std;
import toy;

namespace vk {

auto createRenderPass(VkDevice device, VkFormat format) -> VkRenderPass {
  VkAttachmentDescription color_attachment{
    .format = format,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    // VK_ATTACHMENT_LOAD_OP_LOAD: 保留 attachment 中现有内容
    // VK_ATTACHMENT_LOAD_OP_CLEAR: 将其中内容清理为一个常量
    // VK_ATTACHMENT_LOAD_OP_DONT_CARE: 不在乎
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    // VK_ATTACHMENT_STORE_OP_STORE: 渲染后内容存入内存稍后使用
    // VK_ATTACHMENT_STORE_DONT_CARE: 不在乎
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,

    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    // 开启及结束时 要求 的图像布局
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };
  VkAttachmentReference color_attachment_ref{
    // 引用的 attachment 的索引
    .attachment = 0,
    // 用到该 ref 的 subpass 过程中使用的布局，会自动转换
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  VkSubpassDescription subpass{
    // 还有 compute、 ray tracing 等等
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    // 这里的数组的索引和 着色器里的 layout 数值一一对应
    .pColorAttachments = &color_attachment_ref,
    // pInputAttachments: Attachments that are read from a shader
    // pResolveAttachments: Attachments used for multisampling color attachments
    // pDepthStencilAttachment: Attachment for depth and stencil data
    // pPreserveAttachments: Attachments that are not used by this subpass, but
    // for which the data must be preserved
  };
  // attachment 的 layout 转换是在定义的依赖的中间进行的
  // 如果不主动定义从 VK_SUBPASS_EXTERNAL 到 第一个使用attachment的subpass
  // 的dependency
  // 就会隐式定义一个，VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT到VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
  VkSubpassDependency dependency{
    // VK_SUBPASS_EXTERNAL 代表整个render pass之前提交的命令
    // 而vkQueueSubmit中设置的semaphore wait operation就是 renderpass 之前提交的
    // 但是提交的这个命令的执行阶段是在VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT阶段
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0,
    .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = 0,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };
  VkRenderPassCreateInfo render_pass_create_info{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &color_attachment,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = 1,
    .pDependencies = &dependency,
  };

  return createVkResource(vkCreateRenderPass, device, &render_pass_create_info);
}

void destroyRenderPass(VkRenderPass render_pass, VkDevice device) noexcept {
  vkDestroyRenderPass(device, render_pass, nullptr);
}

auto createShaderModule(std::string_view filepath, VkDevice device)
  -> VkShaderModule {
  std::ifstream istrm{ filepath, std::ios::in | std::ios::binary };
  if (!istrm.is_open()) {
    toy::throwf("Open shader file {} failed!", filepath);
  }
  std::vector<byte> content;
  std::copy(std::istreambuf_iterator<char>{ istrm },
            std::istreambuf_iterator<char>{},
            std::back_inserter(content));
  VkShaderModuleCreateInfo create_info{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = content.size(),
    .pCode = reinterpret_cast<uint32_t*>(content.data()),
  };
  return createVkResource(vkCreateShaderModule, device, &create_info);
}

auto createGraphicsPipeline(
  VkDevice                                         device,
  VkRenderPass                                     render_pass,
  std::span<const VkVertexInputBindingDescription> vertex_binding_descriptions,
  std::span<const VkVertexInputAttributeDescription>
                                         vertex_attribute_descriptions,
  std::span<const VkDescriptorSetLayout> descriptor_set_layouts)
  -> PipelineResource {
  constexpr bool enable_blending_color = false;

  auto vertex_shader = createShaderModule("vert.spv", device);
  auto frag_shader = createShaderModule("frag.spv", device);
  // pSpecializationInfo 可以为 管道 配置着色器的常量，利于编译器优化
  // 类似 constexpr
  std::array<VkPipelineShaderStageCreateInfo, 2> shader_stage_infos = {
    VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vertex_shader,
      .pName = "main",
    },
    VkPipelineShaderStageCreateInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_shader,
      .pName = "main",
    },
  };

  // 很多状态必须提前烘焙到管道中
  // 如果某些状态想要动态设置，可以用 VkPipelineDynamicStateCreateInfo
  // 设置 多个 VkDynamicState
  std::array<VkDynamicState, 2> dynamic_states = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
  };

  VkPipelineDynamicStateCreateInfo dynamic_state_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = dynamic_states.size(),
    .pDynamicStates = dynamic_states.data(),
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount =
      (uint32_t)vertex_binding_descriptions.size(),
    .pVertexBindingDescriptions = vertex_binding_descriptions.data(),
    .vertexAttributeDescriptionCount =
      (uint32_t)vertex_attribute_descriptions.size(),
    .pVertexAttributeDescriptions = vertex_attribute_descriptions.data(),
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    // VK_PRIMITIVE_TOPOLOGY_POINT_LIST
    // VK_PRIMITIVE_TOPOLOGY_LINE_LIST: 不复用的线
    // VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: 首尾相连的线
    // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: 不复用的三角形
    // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    // 下一个三角形的前两条边是上一个三角形的后两条边
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    // 当_STRIP topology下，如果为True，则可以用特殊索引值来 break up 线和三角形
    .primitiveRestartEnable = VK_FALSE,
  };

  VkPipelineViewportStateCreateInfo viewport_state_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    // viewport 和 scissor 是动态的，这里设置为nullptr，等记录命令的时候动态设置
    .viewportCount = 1,
    .pViewports = nullptr,
    .scissorCount = 1,
    .pScissors = nullptr,
  };

  VkPipelineRasterizationStateCreateInfo rasterizer_state_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    // 超过深度范围的会被 clamp， 需要 gpu 支持
    .depthClampEnable = VK_FALSE,
    // 如果开启，geometry 永远不会经过光栅化阶段
    .rasterizerDiscardEnable = VK_FALSE,
    // 如何绘制多边形，除了FILL外皆需要 gpu 支持
    .polygonMode = VK_POLYGON_MODE_FILL,
    // 背面剔除
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    // 深度偏移
    .depthBiasEnable = VK_FALSE,
    // 不为1.0的都需要 gpu 支持
    .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisampling_state_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
  };
  // 颜色混合：将片段着色器返回的颜色与缓冲区中的颜色进行混合
  /**
   * if (blendEnable) {
   *   finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp>
   * (dstColorBlendFactor * oldColor.rgb); finalColor.a = (srcAlphaBlendFactor *
   * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a); } else {
   *   finalColor = newColor;
   * }
   * finalColor = finalColor & colorWriteMask;
   */
  VkPipelineColorBlendAttachmentState color_blend_attachment{
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };
  if constexpr (enable_blending_color) {
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  } else {
    color_blend_attachment.blendEnable = VK_FALSE;
  }

  VkPipelineColorBlendStateCreateInfo color_blend_state_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    // 启用第二种混合方法
    // Combine the old and new value using a bitwise operation
    .logicOpEnable = VK_FALSE,
    .attachmentCount = 1,
    .pAttachments = &color_blend_attachment,
  };

  // 指定 uniform 全局变量
  VkPipelineLayoutCreateInfo pipeline_layout_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = (uint32_t)descriptor_set_layouts.size(),
    .pSetLayouts = descriptor_set_layouts.data(),
  };
  VkPipelineLayout pipeline_layout =
    createVkResource(vkCreatePipelineLayout, device, &pipeline_layout_info);
  VkGraphicsPipelineCreateInfo pipeline_create_info{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = shader_stage_infos.size(),
    .pStages = shader_stage_infos.data(),
    .pVertexInputState = &vertex_input_info,
    .pInputAssemblyState = &input_assembly_info,
    .pTessellationState = nullptr,
    .pViewportState = &viewport_state_info,
    .pRasterizationState = &rasterizer_state_info,
    .pMultisampleState = &multisampling_state_info,
    .pDepthStencilState = nullptr,
    .pColorBlendState = &color_blend_state_info,
    .pDynamicState = &dynamic_state_info,
    .layout = pipeline_layout,
    .renderPass = render_pass,
    .subpass = 0,
    // 相同功能的管道可以共用
    .basePipelineHandle = VK_NULL_HANDLE,
  };

  VkPipeline pipeline = createVkResource(vkCreateGraphicsPipelines,
                                         device,
                                         VK_NULL_HANDLE,
                                         1,
                                         &pipeline_create_info);
  return { vertex_shader, frag_shader, pipeline_layout, pipeline };
}

void destroyGraphicsPipeline(PipelineResource pipeline_resource,
                             VkDevice         device) noexcept {
  vkDestroyPipeline(device, pipeline_resource.pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipeline_resource.pipeline_layout, nullptr);
  vkDestroyShaderModule(device, pipeline_resource.frag_shader, nullptr);
  vkDestroyShaderModule(device, pipeline_resource.vertex_shader, nullptr);
}

auto createFramebuffers(VkRenderPass                 render_pass,
                        VkDevice                     device,
                        VkExtent2D                   extent,
                        std::span<const VkImageView> image_views)
  -> std::vector<VkFramebuffer> {
  return image_views |
         views::transform([render_pass, device, extent](auto image_view) {
           VkFramebufferCreateInfo create_info{
             .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
             .renderPass = render_pass,
             .attachmentCount = 1,
             .pAttachments = &image_view,
             .width = extent.width,
             .height = extent.height,
             .layers = 1,
           };
           return createVkResource(vkCreateFramebuffer, device, &create_info);
         }) |
         ranges::to<std::vector>();
}

void destroyFramebuffers(std::span<const VkFramebuffer> framebuffers,
                         VkDevice                       device) noexcept {
  for (auto framebuffer : framebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }
}

auto createCommandPool(VkDevice device, uint32_t graphic_family_index)
  -> VkCommandPool {
  VkCommandPoolCreateInfo pool_create_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：允许重置单个command
    // buffer，否则就要重置命令池里的所有buffer
    // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT: 命令缓冲区会很频繁的记录新命令
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = graphic_family_index,
  };
  return createVkResource(vkCreateCommandPool, device, &pool_create_info);
}

auto allocateCommandBuffers(VkDevice      device,
                            VkCommandPool command_pool,
                            uint32_t count) -> std::vector<VkCommandBuffer> {
  VkCommandBufferAllocateInfo cbuffer_alloc_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = command_pool,
    // VK_COMMAND_BUFFER_LEVEL_PRIMARY: 主缓冲区，类似于main
    // VK_COMMAND_BUFFER_LEVEL_SECONDARY: 次缓冲区，可复用，类似于其他函数
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = count,
  };
  std::vector<VkCommandBuffer> command_buffers(count);
  checkVkResult(vkAllocateCommandBuffers(
                  device, &cbuffer_alloc_info, command_buffers.data()),
                "command buffer");
  return command_buffers;
}

void destroyCommandPool(VkCommandPool command_pool, VkDevice device) noexcept {
  vkDestroyCommandPool(device, command_pool, nullptr);
}

void freeCommandBuffer(VkCommandBuffer command_buffer,
                       VkDevice        device,
                       VkCommandPool   command_pool) noexcept {
  vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}

} // namespace vk