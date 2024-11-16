module;
#include <toy.h>
module render.pipeline;

import <vulkan_config.h>;

namespace rd {

auto PipelineDrawer::forRecord(
  VkCommandBuffer                  cmdbuf,
  VkPipeline                       pipeline,
  VkPipelineLayout                 layout,
  VkExtent2D                       extent,
  VertexInfo                       vertex_info,
  std::span<VkPushConstantRange>   push_constants,
  std::span<VkDescriptorSetLayout> dset_layouts
) -> PipelineDrawer {
  return PipelineDrawer{
    cmdbuf,       pipeline, layout,  extent,  vertex_info, push_constants,
    dset_layouts, nullptr,  nullptr, nullptr, RECORD,
  };
}

auto PipelineDrawer::forCollect(
  std::vector<Buffer*>*      vertex_buffers,
  std::vector<Buffer*>*      index_buffers,
  std::vector<ResourceSet*>* resource_sets
) -> PipelineDrawer {
  return PipelineDrawer{
    VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, {}, {}, {}, {}, vertex_buffers,
    index_buffers,  resource_sets,  GET_RESOURCES,
  };
}

void PipelineDrawer::bindVertexBuffer(VertexBuffer* vertex_buffer) {
  if (_execute_type == RECORD) {
    TOY_CHECK_ASSERT(vertex_buffer->getVertexInfo() == _vertex_info);
    auto offset = VkDeviceSize{ 0 };
    auto buffer = vertex_buffer->get();
    vkCmdBindVertexBuffers(_cmdbuf, 0, 1, &buffer, &offset);
    if (!_index) {
      _vertex_count = vertex_buffer->getVertexNumber();
    }
  } else {
    _vertex_buffers->push_back(vertex_buffer);
  }
}

void PipelineDrawer::bindIndexBuffer(IndexBuffer* index_buffer) {
  if (_execute_type == RECORD) {
    vkCmdBindIndexBuffer(_cmdbuf, *index_buffer, 0, index_buffer->getIndexType());
    _vertex_count = index_buffer->getIndexNumber();
    _index = true;
  } else {
    _index_buffers->push_back(index_buffer);
  }
}

void PipelineDrawer::bindResourceSet(uint32 index, ResourceSet* resource_set) {
  if (_execute_type == RECORD) {
    TOY_CHECK_ASSERT(_dset_layouts[index] == resource_set->getLayout());
    auto handle = resource_set->get();
    vkCmdBindDescriptorSets(
      _cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, _layout, index, 1, &handle, 0, nullptr
    );
  } else {
    _resource_sets->push_back(resource_set);
  }
}

void PipelineDrawer::bindPushConstant(VkShaderStageFlags stage, std::span<std::byte const> data) {
  if (_execute_type == RECORD) {
    auto opt = toy::findIf(_push_constants, [&](auto& range) { return range.stageFlags == stage; });
    TOY_CHECK_ASSERT(opt.has_value());
    TOY_CHECK_ASSERT(data.size() == opt->size, data.size(), opt->size);
    vkCmdPushConstants(_cmdbuf, _layout, opt->stageFlags, opt->offset, opt->size, data.data());
  }
}

void PipelineDrawer::setStencilReference(uint32 reference) {
  if (_execute_type == RECORD) {
    vkCmdSetStencilReference(_cmdbuf, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
  }
}

void PipelineDrawer::draw() {
  if (_execute_type == RECORD) {
    if (_index) {
      vkCmdDrawIndexed(_cmdbuf, _vertex_count, 1, 0, 0, 0);
    } else {
      vkCmdDraw(_cmdbuf, _vertex_count, 1, 0, 0);
    }
  }
}

void Pipeline::record(VkCommandBuffer cmdbuf, VkExtent2D extent, PipelineRecorder recorder) {
  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, getPipeline());
  // 定义了 viewport 到缓冲区的变换
  auto viewport = VkViewport{
    .x = 0,
    .y = 0,
    .width = static_cast<float>(extent.width),
    .height = static_cast<float>(extent.height),
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };
  vkCmdSetViewport(cmdbuf, 0, 1, &viewport);
  // 定义了缓冲区实际存储像素的区域
  auto scissor = VkRect2D{
    .offset = { .x = 0, .y = 0 },
    .extent = extent,
  };
  vkCmdSetScissor(cmdbuf, 0, 1, &scissor);
  recorder(PipelineDrawer::forRecord(
    cmdbuf, getPipeline(), getLayout(), extent, _vertex_info, _push_constants, _dset_layouts
  ));
}

} // namespace rd