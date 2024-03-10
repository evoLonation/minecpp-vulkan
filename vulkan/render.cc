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
      .finalLayout = final_layout,
    };
  };
  auto attachments =
    std::array{ get_attachment(color_format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),
                get_attachment(depth_format, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) };
  auto color_attach_ref = VkAttachmentReference{
    // 引用的 attachment 的索引
    .attachment = 0,
    // 用到该 ref 的 subpass 过程中使用的布局，会自动转换
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  auto depth_attach_ref = VkAttachmentReference{
    .attachment = 1,
    .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
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
  // For color attachment dependency
  // vkQueueSubmit中设置了image presenting -> color attachment output（外部）的执行依赖
  // 因此这里只需添加 color attachment output（外部）-> color attachment
  // output（内部）的内存依赖和布局转换
  auto src_color_scope = ScopeInfo{
    .stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .access_mask = 0,
  };
  auto dst_color_scope = ScopeInfo{
    .stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };
  // For depth attachment dependency
  // Multi workers use one depth image, so must sync last worker and current worker with stage
  // early/late fragment test stage and read/write depth attach access.
  auto src_depth_scope = ScopeInfo{
    .stage_mask =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    .access_mask =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
  };
  auto dst_depth_scope = src_depth_scope;

  // attachment 的 layout 转换是在定义的依赖的中间进行的
  // 如果不主动定义从 VK_SUBPASS_EXTERNAL 到 第一个使用attachment的subpass
  // 的dependency
  // 就会隐式定义一个，VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT到VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
  auto dependency = VkSubpassDependency{
    // VK_SUBPASS_EXTERNAL 代表整个render pass之前提交的命令
    // 而vkQueueSubmit中设置的semaphore wait operation就是 renderpass 之前提交的
    // 但是提交的这个命令的执行阶段是在VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT阶段
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0,
    // Multi workers use one depth image, so must sync last worker and current worker with stage
    // early/late fragment test stage and read/write depth attach access.
    .srcStageMask = src_color_scope.stage_mask | src_depth_scope.stage_mask,
    .dstStageMask = dst_color_scope.stage_mask | dst_depth_scope.stage_mask,
    .srcAccessMask = src_color_scope.access_mask | src_depth_scope.access_mask,
    .dstAccessMask = dst_color_scope.access_mask | dst_depth_scope.access_mask,
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
  std::span<const VkVertexInputBindingDescription>   vertex_binding_descriptions,
  std::span<const VkVertexInputAttributeDescription> vertex_attribute_descriptions,
  std::span<const VkDescriptorSetLayout>             descriptor_set_layouts
) -> PipelineResource {
  constexpr bool enable_blending_color = false;

  auto vertex_shader = createShaderModule("hello.vert", device);
  auto frag_shader = createShaderModule("hello.frag", device);
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
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
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

void recordCommandBuffer(
  VkCommandBuffer                  command_buffer,
  VkRenderPass                     render_pass,
  VkPipeline                       graphics_pipeline,
  VkExtent2D                       extent,
  VkFramebuffer                    framebuffer,
  VertexBuffer&                    vertex_buffer,
  IndexBuffer&                     index_buffer,
  VkPipelineLayout                 pipeline_layout,
  std::span<const VkDescriptorSet> descriptor_sets
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
  vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
  // 定义了 viewport 到缓冲区的变换
  VkViewport viewport{
    .x = 0,
    .y = 0,
    .width = (float)extent.width,
    .height = (float)extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };
  vkCmdSetViewport(command_buffer, 0, 1, &viewport);
  // 定义了缓冲区实际存储像素的区域
  VkRect2D scissor{
    .offset = { .x = 0, .y = 0 },
    .extent = extent,
  };
  vkCmdSetScissor(command_buffer, 0, 1, &scissor);
  auto offsets = std::array<VkDeviceSize, 1>{ 0 };
  vertex_buffer.recordBind(command_buffer);
  index_buffer.recordBind(command_buffer);
  vkCmdBindDescriptorSets(
    command_buffer,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    pipeline_layout,
    // firstSet: 对应着色器中的layout(set=0)
    0,
    descriptor_sets.size(),
    descriptor_sets.data(),
    0,
    nullptr
  );
  vkCmdDrawIndexed(command_buffer, index_buffer.getIndicesSize(), 1, 0, 0, 0);
  vkCmdEndRenderPass(command_buffer);
}

} // namespace vk