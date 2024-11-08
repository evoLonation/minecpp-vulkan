module;
#include <toy.h>
module render.pipeline;

import toy;

import render.shader_code;

import <vulkan_config.h>;

namespace rd {

auto createShaderModule(std::string_view filename) -> rs::ShaderModule {
  auto content = get_shader_code(filename);
  auto create_info = VkShaderModuleCreateInfo{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = content.size(),
    .pCode = reinterpret_cast<const uint32*>(content.data()),
  };
  return rs::ShaderModule{ create_info };
}

auto createPipelineLayout(std::span<VkDescriptorSetLayout const> dset_layouts) //
  -> rs::PipelineLayout {
  // 指定 uniform 全局变量
  auto pipeline_layout_info = VkPipelineLayoutCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = static_cast<uint32>(dset_layouts.size()),
    .pSetLayouts = dset_layouts.data(),
  };
  return rs::PipelineLayout{ pipeline_layout_info };
}

// todo: add depth option
auto createGraphicsPipeline(
  VkRenderPass                                       render_pass,
  uint32                                             subpass_i,
  VkShaderModule                                     vertex_shader,
  VkShaderModule                                     frag_shader,
  VkPipelineLayout                                   layout,
  VkPrimitiveTopology                                topology,
  VkSampleCountFlagBits                              sample_count,
  std::optional<StencilOption>                       stencil_option,
  std::optional<DepthOption>                         depth_option,
  std::span<VkVertexInputBindingDescription const>   vertex_bindings,
  std::span<VkVertexInputAttributeDescription const> vertex_attribs
) -> rs::Pipeline {
  constexpr bool enable_blending_color = false;

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
  auto dynamic_states = std::vector<VkDynamicState>{
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
  };
  if (stencil_option
        .transform([](auto& x) { return x.dynamic_reference; }) //
        .value_or(false)) {
    dynamic_states.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
  }

  auto dynamic_state_info = VkPipelineDynamicStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = (uint32)dynamic_states.size(),
    .pDynamicStates = dynamic_states.data(),
  };

  auto vertex_input_info = VkPipelineVertexInputStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = static_cast<uint32>(vertex_bindings.size()),
    .pVertexBindingDescriptions = vertex_bindings.data(),
    .vertexAttributeDescriptionCount = static_cast<uint32>(vertex_attribs.size()),
    .pVertexAttributeDescriptions = vertex_attribs.data(),
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
    // 背面剔除, 指定要剔除的面
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    // 深度偏移
    .depthBiasEnable = VK_FALSE,
    // 不为1.0的都需要 gpu 支持
    .lineWidth = 1.0f,
  };

  auto multisampling_state_info = VkPipelineMultisampleStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = sample_count,
    .sampleShadingEnable = VK_FALSE,
  };

  auto depst_info = VkPipelineDepthStencilStateCreateInfo{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .depthBoundsTestEnable = VK_FALSE,
    .minDepthBounds = 0.0f,
    .maxDepthBounds = 1.0f,
  };

  if (depth_option.has_value()) {
    // new fragment are compared to the depth buffer to see if they should by discard
    depst_info.depthTestEnable = VK_TRUE;
    // If replace the depth buffer with new fragment if test success
    depst_info.depthWriteEnable = depth_option->overwrite;
    // Lower depth of fragment is closer
    depst_info.depthCompareOp = depth_option->compare_op;
  } else {
    depst_info.depthTestEnable = VK_FALSE;
  }

  if (stencil_option.has_value()) {
    depst_info.stencilTestEnable = VK_TRUE;
    depst_info.front = stencil_option->front;
    depst_info.back = stencil_option->back;
  } else {
    depst_info.stencilTestEnable = VK_FALSE;
  }

  // TOY_DEBUG(
  //   depst_info.depthTestEnable, depst_info.depthWriteEnable, (int)depst_info.depthCompareOp
  // );

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
    .pDepthStencilState = &depst_info,
    .pColorBlendState = &color_blend_state_info,
    .pDynamicState = &dynamic_state_info,
    .layout = layout,
    .renderPass = render_pass,
    .subpass = subpass_i,
    // 相同功能的管道可以共用
    .basePipelineHandle = VK_NULL_HANDLE,
  };

  return std::move(
    rs::GraphicsPipelineFactory::create(VK_NULL_HANDLE, std::span{ &pipeline_create_info, 1 })[0]
  );
}

Pipeline::Pipeline(const PipelineInfo& info) {
  _vertex_shader = createShaderModule(info.vertex_shader_name);
  _frag_shader = createShaderModule(info.frag_shader_name);
  _layout = createPipelineLayout(info.dset_layouts);
  _pipeline = createGraphicsPipeline(
    info.render_pass,
    info.subpass_i,
    _vertex_shader,
    _frag_shader,
    _layout,
    info.topology,
    info.sample_count,
    info.stencil_option,
    info.depth_option,
    std::array{ *info.vertex_info.binding_description },
    info.vertex_info.attribute_descriptions
  );
}

} // namespace rd