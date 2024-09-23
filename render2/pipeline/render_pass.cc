module render.vk.render_pass;

import "vulkan_config.h";
import render.vk.device;

namespace rd::vk {

DescriptorPool::DescriptorPool(
  uint32_t set_count, std::span<const VkDescriptorPoolSize> type_counts
) {
  auto pool_create_info = VkDescriptorPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT: 允许freeDescriptorSets
    .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    // 会分配描述符集的最大数量
    .maxSets = set_count,
    .poolSizeCount = static_cast<uint32_t>(type_counts.size()),
    .pPoolSizes = type_counts.data(),
  };
  rs::DescriptorPool::operator=({ pool_create_info });
}

DescriptorSet::DescriptorSet(
  const DescriptorPool& pool, const Pipeline& pipeline, uint32_t set_id
) {
  auto dset_layout = pipeline.descriptor_set_layouts()[set_id].get();
  auto allocate_info = VkDescriptorSetAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = pool,
    .descriptorSetCount = 1,
    .pSetLayouts = &dset_layout,
  };
  _dsets = { allocate_info };
}

auto Descriptor::operator=(std::initializer_list<std::reference_wrapper<Buffer const>> resources
) -> Descriptor& {
  auto buffer_infos = resources | views::transform([&](Buffer const& buffer) {
                        return VkDescriptorBufferInfo{
                          .buffer = buffer,
                          .offset = 0,
                          .range = VK_WHOLE_SIZE,
                        };
                      }) |
                      ranges::to<std::vector>();
  auto write_info = VkWriteDescriptorSet{
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = _dset->get(),
    .dstBinding = _binding,
    // 数组起始索引
    .dstArrayElement = 0,
    .descriptorCount = static_cast<uint32_t>(resources.size()),
    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    .pBufferInfo = buffer_infos.data(),
  };
  vkUpdateDescriptorSets(Device::getInstance(), 1, &write_info, 0, nullptr);
  return *this;
}
auto Descriptor::operator=(
  std::initializer_list<std::reference_wrapper<SampledTexture const>> resources
) -> Descriptor& {
  auto image_infos = resources | views::transform([&](SampledTexture const& texture) {
                       return VkDescriptorImageInfo{
                         .sampler = texture.sampler(),
                         .imageView = texture.image_view(),
                         .imageLayout = texture.getLayout(),
                       };
                     }) |
                     ranges::to<std::vector>();
  auto write_info = VkWriteDescriptorSet{
    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    .dstSet = _dset->get(),
    .dstBinding = _binding,
    // 数组起始索引
    .dstArrayElement = 0,
    .descriptorCount = static_cast<uint32_t>(resources.size()),
    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    .pImageInfo = image_infos.data(),
  };
  vkUpdateDescriptorSets(Device::getInstance(), 1, &write_info, 0, nullptr);
  return *this;
}

Framebuffer::Framebuffer(
  RenderPass& render_pass, VkExtent2D extent, std::span<const VkImageView> image_views
) {
  _extent = extent;
  auto create_info = VkFramebufferCreateInfo{
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass = render_pass.render_pass(),
    .attachmentCount = static_cast<uint32_t>(image_views.size()),
    .pAttachments = image_views.data(),
    .width = extent.width,
    .height = extent.height,
    .layers = 1,
  };
  rs::Framebuffer::operator=(create_info);
}

void Pipeline::Recorder::init() {
  vkCmdBindPipeline(_cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);
  // 定义了 viewport 到缓冲区的变换
  VkViewport viewport{
    .x = 0,
    .y = 0,
    .width = static_cast<float>(_extent.width),
    .height = static_cast<float>(_extent.height),
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };
  vkCmdSetViewport(_cmdbuf, 0, 1, &viewport);
  // 定义了缓冲区实际存储像素的区域
  VkRect2D scissor{
    .offset = { .x = 0, .y = 0 },
    .extent = _extent,
  };
  vkCmdSetScissor(_cmdbuf, 0, 1, &scissor);
}

void Pipeline::Recorder::draw() { vkCmdDrawIndexed(_cmdbuf, _index_count, 1, 0, 0, 0); }

auto Pipeline::Recorder::DescriptorSetBinding::DescriptorSetBindingTarget::operator=(
  DescriptorSet& descriptor_set
) -> DescriptorSetBindingTarget& {
  auto handle = descriptor_set.get();
  vkCmdBindDescriptorSets(
    _parent->_cmdbuf,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    _parent->_pipeline_layout,
    // firstSet: 对应着色器中的layout(set=0)
    _index,
    1,
    &handle,
    0,
    nullptr
  );
  return *this;
}

auto Pipeline::Recorder::VertexBufferBinding::operator=(VertexBuffer& vertex_buffer
) -> VertexBufferBinding& {
  auto offset = VkDeviceSize{ 0 };
  auto buffer = vertex_buffer.get();
  vkCmdBindVertexBuffers(_cmdbuf, 0, 1, &buffer, &offset);
  return *this;
}

auto Pipeline::Recorder::IndexBufferBinding::operator=(IndexBuffer& index_buffer
) -> IndexBufferBinding& {
  vkCmdBindIndexBuffer(_cmdbuf, index_buffer, 0, index_buffer.getIndexType());
  _recorder->_index_count = index_buffer.getIndexNumber();
  return *this;
}

void RenderPass::recordDraw(
  VkCommandBuffer cmdbuf, Framebuffer& framebuffer, std::span<const VkClearValue> clear_values
) {
  VkRenderPassBeginInfo render_pass_begin_info {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = _render_pass,
    .framebuffer = framebuffer,
    .renderArea = {
      .offset = {0, 0},
      .extent = framebuffer.extent(),
    },
    .clearValueCount = static_cast<uint32_t>(clear_values.size()),
    .pClearValues = clear_values.data(),
  };
  // VK_SUBPASS_CONTENTS_INLINE: render pass的command被嵌入主缓冲区
  // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS: render pass 命令
  // 将会从次缓冲区执行
  vkCmdBeginRenderPass(cmdbuf, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  for (auto& pipeline : _pipelines) {
    auto recorder = Pipeline::Recorder{
      cmdbuf, pipeline.pipeline(), pipeline.pipeline_layout(), framebuffer.extent()
    };
    pipeline.recorder(recorder);
  }
  vkCmdEndRenderPass(cmdbuf);
}

} // namespace rd::vk