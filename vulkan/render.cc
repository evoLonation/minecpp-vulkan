module vulkan.render;

import "vulkan_config.h";
import vulkan.tool;
import vulkan.buffer;
import vulkan.shader_code;
import vulkan.sync;
import toy;

namespace vk {

auto createRenderPass(VkDevice device, VkFormat color_format, VkFormat depth_format) -> RenderPass {
  auto get_attachment = [](VkFormat format, VkImageLayout final_layout) {
    return VkAttachmentDescription{
      .format = format,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      /**
       * @brief load op: define load operation behavior
       * the load op happen in VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT(color attachment) or
       * VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT(depth attachment)
       * and happen before any command which access the sample in the render pass
       */
      // VK_ATTACHMENT_LOAD_OP_LOAD: 保留 attachment 中现有内容
      // VK_ATTACHMENT_LOAD_OP_CLEAR: 将其中内容清理为一个常量
      // VK_ATTACHMENT_LOAD_OP_DONT_CARE: 不在乎
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      /**
       * @brief store op: define store operation behavior
       * the store op happen in VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT(color attachment) or
       * VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT(depth attachment)
       * and happen after any command which access the sample in the render pass
       */
      // VK_ATTACHMENT_STORE_OP_STORE: 渲染后内容存入内存稍后使用
      // VK_ATTACHMENT_STORE_DONT_CARE: 不在乎
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,

      .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      // 开启及结束时 要求 的图像布局
      // UNDEINFED init layout use with CLEAR load op together
      .initialLayout = getLayout(ImageUse::UNDEFINED),
      .finalLayout = final_layout,
    };
  };
  auto attachments = std::array{
    get_attachment(color_format, getLayout(ImageUse::PRESENT)),
    get_attachment(depth_format, getLayout(ImageUse::DEPTH_ATTACHMENT)),
  };
  auto color_attach_ref = VkAttachmentReference{
    // 引用的 attachment 的索引
    .attachment = 0,
    // 用到该 ref 的 subpass 过程中使用的布局，会自动转换
    .layout = getLayout(ImageUse::COLOR_ATTACHMENT),
  };
  auto depth_attach_ref = VkAttachmentReference{
    .attachment = 1,
    .layout = getLayout(ImageUse::DEPTH_ATTACHMENT),
  };
  auto subpass = VkSubpassDescription{
    // 还有 compute、 ray tracing 等等
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    // 这里的数组的索引和 着色器里的 layout 数值一一对应
    .pColorAttachments = &color_attach_ref,
    // pInputAttachments: Attachments that are read from a shader
    // pResolveAttachments: Attachments used for multisampling color attachments
    // pDepthStencilAttachment: Attachment for depth and stencil data
    .pDepthStencilAttachment = &depth_attach_ref,
    // pPreserveAttachments: Attachments that are not used by this subpass, but
    // for which the data must be preserved
  };
  // For src scope :
  // access_mask: 0, because the load op is CLEAR, meaning we don't need previous access is
  // available
  // stage_mask: depth attachment wait previous flight and output attachment wait outside
  // For dst scope we need define visible operation for depth and color operation
  auto src_scope =
    Scope{ vk::ImageUse::DEPTH_ATTACHMENT, false } | Scope{ vk::ImageUse::COLOR_ATTACHMENT, false };
  src_scope.exeDep();
  auto dst_scope =
    Scope{ vk::ImageUse::COLOR_ATTACHMENT, true } | Scope{ vk::ImageUse::DEPTH_ATTACHMENT, true };
  auto dependency = VkSubpassDependency{
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0,
    .srcStageMask = src_scope.stage_mask,
    .dstStageMask = dst_scope.stage_mask,
    .srcAccessMask = src_scope.access_mask,
    .dstAccessMask = dst_scope.access_mask,
  };
  auto render_pass_create_info = VkRenderPassCreateInfo{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = attachments.size(),
    .pAttachments = attachments.data(),
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = 1,
    .pDependencies = &dependency,
  };

  return RenderPass{ device, render_pass_create_info };
}

auto createFramebuffer(
  VkRenderPass render_pass,
  VkDevice     device,
  VkExtent2D   extent,
  VkImageView  color_image,
  VkImageView  depth_image
) -> Framebuffer {
  auto attachments = std::array{ color_image, depth_image };
  auto create_info = VkFramebufferCreateInfo{
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass = render_pass,
    .attachmentCount = attachments.size(),
    .pAttachments = attachments.data(),
    .width = extent.width,
    .height = extent.height,
    .layers = 1,
  };
  return Framebuffer{ device, create_info };
}

auto createShaderModule(std::string_view filename, VkDevice device) -> ShaderModule {
  auto content = get_shader_code(filename);
  auto create_info = VkShaderModuleCreateInfo{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = content.size(),
    .pCode = reinterpret_cast<const uint32_t*>(content.data()),
  };
  return ShaderModule{ device, create_info };
}

auto createGraphicsPipeline(
  VkDevice                                           device,
  VkRenderPass                                       render_pass,
  VkPrimitiveTopology                                topology,
  std::string_view                                   vertex_shader_name,
  std::string_view                                   frag_shader_name,
  std::span<const VkVertexInputBindingDescription>   vertex_binding_descriptions,
  std::span<const VkVertexInputAttributeDescription> vertex_attribute_descriptions,
  std::span<const VkDescriptorSetLayout>             descriptor_set_layouts
) -> PipelineResource {
  constexpr bool enable_blending_color = false;

  auto vertex_shader = createShaderModule(vertex_shader_name, device);
  auto frag_shader = createShaderModule(frag_shader_name, device);
  // pSpecializationInfo 可以为 管道 配置着色器的常量，利于编译器优化
  // 类似 constexpr
  auto shader_stage_infos = std::array{
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
  auto dynamic_states = std::array{
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
  };

  auto dynamic_state_info = VkPipelineDynamicStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = dynamic_states.size(),
    .pDynamicStates = dynamic_states.data(),
  };

  auto vertex_input_info = VkPipelineVertexInputStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = (uint32_t)vertex_binding_descriptions.size(),
    .pVertexBindingDescriptions = vertex_binding_descriptions.data(),
    .vertexAttributeDescriptionCount = (uint32_t)vertex_attribute_descriptions.size(),
    .pVertexAttributeDescriptions = vertex_attribute_descriptions.data(),
  };

  auto input_assembly_info = VkPipelineInputAssemblyStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    // VK_PRIMITIVE_TOPOLOGY_POINT_LIST
    // VK_PRIMITIVE_TOPOLOGY_LINE_LIST: 不复用的线
    // VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: 首尾相连的线
    // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: 不复用的三角形
    // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
    // 下一个三角形的前两条边是上一个三角形的后两条边
    .topology = topology,
    // 当_STRIP topology下，如果为True，则可以用特殊索引值来 break up 线和三角形
    .primitiveRestartEnable = VK_FALSE,
  };

  auto viewport_state_info = VkPipelineViewportStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    // viewport 和 scissor 是动态的，这里设置为nullptr，等记录命令的时候动态设置
    .viewportCount = 1,
    .pViewports = nullptr,
    .scissorCount = 1,
    .pScissors = nullptr,
  };

  auto rasterizer_state_info = VkPipelineRasterizationStateCreateInfo{
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

  auto multisampling_state_info = VkPipelineMultisampleStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
  };

  auto depth_stencil_info = VkPipelineDepthStencilStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    // new fragment are compared to the depth buffer to see if they should by discard
    .depthTestEnable = VK_TRUE,
    // If replace the depth buffer with new fragment if test success
    .depthWriteEnable = VK_TRUE,
    // Lower depth of fragment is closer
    .depthCompareOp = VK_COMPARE_OP_LESS,
    .depthBoundsTestEnable = VK_FALSE,
    .stencilTestEnable = VK_FALSE,
    .front = {},
    .back = {},
    .minDepthBounds = 0.0f,
    .maxDepthBounds = 1.0f,
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
  auto color_blend_attachment = VkPipelineColorBlendAttachmentState{
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };
  if constexpr (enable_blending_color) {
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  } else {
    color_blend_attachment.blendEnable = VK_FALSE;
  }

  auto color_blend_state_info = VkPipelineColorBlendStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    // 启用第二种混合方法
    // Combine the old and new value using a bitwise operation
    .logicOpEnable = VK_FALSE,
    .attachmentCount = 1,
    .pAttachments = &color_blend_attachment,
  };

  // 指定 uniform 全局变量
  auto pipeline_layout_info = VkPipelineLayoutCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = (uint32_t)descriptor_set_layouts.size(),
    .pSetLayouts = descriptor_set_layouts.data(),
  };
  auto pipeline_layout = PipelineLayout{ device, pipeline_layout_info };

  auto pipeline_create_info = VkGraphicsPipelineCreateInfo{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .stageCount = shader_stage_infos.size(),
    .pStages = shader_stage_infos.data(),
    .pVertexInputState = &vertex_input_info,
    .pInputAssemblyState = &input_assembly_info,
    .pTessellationState = nullptr,
    .pViewportState = &viewport_state_info,
    .pRasterizationState = &rasterizer_state_info,
    .pMultisampleState = &multisampling_state_info,
    .pDepthStencilState = &depth_stencil_info,
    .pColorBlendState = &color_blend_state_info,
    .pDynamicState = &dynamic_state_info,
    .layout = pipeline_layout,
    .renderPass = render_pass,
    .subpass = 0,
    // 相同功能的管道可以共用
    .basePipelineHandle = VK_NULL_HANDLE,
  };

  auto pipeline = std::move(GraphicsPipelineFactory::create(
    device, VK_NULL_HANDLE, std::span{ &pipeline_create_info, 1 }
  )[0]);
  return { std::move(vertex_shader),
           std::move(frag_shader),
           std::move(pipeline_layout),
           std::move(pipeline) };
}

void recordRenderPass(
  VkCommandBuffer                      cmdbuf,
  VkRenderPass                         render_pass,
  VkFramebuffer                        framebuffer,
  VkExtent2D                           extent,
  std::function<void(VkCommandBuffer)> recorder
) {
  auto color_clear = VkClearValue{ .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } };
  auto depth_clear = VkClearValue{ .depthStencil = { .depth = 1.0f, .stencil = 0 } };
  auto clear_values = std::array{ color_clear, depth_clear };
  VkRenderPassBeginInfo render_pass_begin_info {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = render_pass,
    .framebuffer = framebuffer,
    .renderArea = {
      .offset = {0, 0},
      .extent = extent,
    },
    .clearValueCount = clear_values.size(),
    .pClearValues = clear_values.data(),
  };
  // VK_SUBPASS_CONTENTS_INLINE: render pass的command被嵌入主缓冲区
  // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS: render pass 命令
  // 将会从次缓冲区执行
  vkCmdBeginRenderPass(cmdbuf, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  recorder(cmdbuf);
  vkCmdEndRenderPass(cmdbuf);
}

void recordDrawUnits(
  VkCommandBuffer           cmdbuf,
  VkPipeline                graphics_pipeline,
  VkExtent2D                extent,
  VkPipelineLayout          pipeline_layout,
  std::span<const DrawUnit> draw_units
) {
  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
  // 定义了 viewport 到缓冲区的变换
  VkViewport viewport{
    .x = 0,
    .y = 0,
    .width = (float)extent.width,
    .height = (float)extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };
  vkCmdSetViewport(cmdbuf, 0, 1, &viewport);
  // 定义了缓冲区实际存储像素的区域
  VkRect2D scissor{
    .offset = { .x = 0, .y = 0 },
    .extent = extent,
  };
  vkCmdSetScissor(cmdbuf, 0, 1, &scissor);
  for (auto& [vertex_buffer, index_buffer, count, descriptor_sets] : draw_units) {
    auto offset = (VkDeviceSize)0;
    vkCmdBindVertexBuffers(cmdbuf, 0, 1, &vertex_buffer, &offset);
    vkCmdBindIndexBuffer(cmdbuf, index_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets(
      cmdbuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipeline_layout,
      // firstSet: 对应着色器中的layout(set=0)
      0,
      descriptor_sets.size(),
      descriptor_sets.data(),
      0,
      nullptr
    );
    vkCmdDrawIndexed(cmdbuf, count, 1, 0, 0, 0);
  }
}

} // namespace vk