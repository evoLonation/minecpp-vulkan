module;
#include <toy.h>
module render.render_pass;

import <vulkan_config.h>;

import render.sync;
import render.executor;
import render.tracker;

namespace rd {

void recordRenderPass(
  VkCommandBuffer                      cmdbuf,
  VkRenderPass                         render_pass,
  VkFramebuffer                        framebuffer,
  VkExtent2D                           extent,
  std::span<VkClearValue const>        clear_values,
  std::function<void(VkCommandBuffer)> recorder
) {
  auto render_pass_begin_info = VkRenderPassBeginInfo{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = render_pass,
    .framebuffer = framebuffer,
    .renderArea = VkRect2D{ .offset = VkOffset2D{ 0, 0 }, .extent = extent },
    .clearValueCount = static_cast<uint32>(clear_values.size()),
    .pClearValues = clear_values.data(),
  };
  // VK_SUBPASS_CONTENTS_INLINE: render pass的command被嵌入主缓冲区
  // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS: render pass 命令
  // 将会从次缓冲区执行
  vkCmdBeginRenderPass(cmdbuf, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  recorder(cmdbuf);
  vkCmdEndRenderPass(cmdbuf);
}

void RenderPass::record(
  std::vector<CommandBatch>                        batches,
  std::span<FrameImageManager* const>              images,
  std::vector<VkClearValue>                        clear_values,
  std::function<void(VkCommandBuffer, VkExtent2D)> pipeline_recorder
) {
  auto [framebuffer, extent] = FramebufferPool::getInstance().getFramebuffer(get(), images);

  auto waitables_keep_lifetime = std::list<Waitable>{};
  for (auto [image, info] : views::zip(images, getSyncInfos())) {
    auto sync = image->getTracker().syncScope(
      Scope{ .stage_mask = info.initial_stage }, _executor->getFamily(), info.initial_layout
    );
    if (auto* ctx = std::get_if<BarrierRecorder>(&sync)) {
      batches.push_back(CommandBatch{ std::move(*ctx) });
    } else if (auto* ctx = std::get_if<FamilyTransferRecorder>(&sync)) {
      auto waitable = ctx->executeRelease();
      waitables_keep_lifetime.push_back(std::move(waitable));
      batches.push_back(ctx->toAcquireBatch(&waitables_keep_lifetime.back()));
    }
  }
  using namespace std::placeholders;
  batches.push_back(CommandBatch{ [&](VkCommandBuffer cmdbuf) {
    recordRenderPass(
      cmdbuf, get(), framebuffer, extent, clear_values, std::bind(pipeline_recorder, _1, extent)
    );
  } });
  _executor->submit(batches);

  for (auto [image, info] : views::zip(images, getSyncInfos())) {
    image->getTracker().setNewScope(
      Scope{ .stage_mask = info.final_stage }, _executor->getFamily(), info.final_layout
    );
  }
}

auto PipelineDrawer::forRecord(
  VkCommandBuffer cmdbuf, VkPipeline pipeline, VkPipelineLayout layout, VkExtent2D extent
) -> PipelineDrawer {
  return PipelineDrawer{ cmdbuf, pipeline, layout, extent, {}, nullptr, nullptr, nullptr, RECORD };
}

auto PipelineDrawer::forGetResources(
  std::vector<VkDescriptorSetLayout> dset_layouts,
  std::vector<Buffer*>*              vertex_buffers,
  std::vector<Buffer*>*              index_buffers,
  std::vector<ResourceSet*>*         resource_sets
) -> PipelineDrawer {
  return PipelineDrawer{
    VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, {}, dset_layouts, vertex_buffers,
    index_buffers,  resource_sets,  GET_RESOURCES,
  };
}

void PipelineDrawer::bindVertexBuffer(VertexBuffer* vertex_buffer) {
  if (_execute_type == RECORD) {
    auto offset = VkDeviceSize{ 0 };
    auto buffer = vertex_buffer->get();
    vkCmdBindVertexBuffers(_cmdbuf, 0, 1, &buffer, &offset);
  } else {
    _vertex_buffers->push_back(vertex_buffer);
  }
}

void PipelineDrawer::bindIndexBuffer(IndexBuffer* index_buffer) {
  if (_execute_type == RECORD) {
    vkCmdBindIndexBuffer(_cmdbuf, *index_buffer, 0, index_buffer->getIndexType());
    _index_count = index_buffer->getIndexNumber();
  } else {
    _index_buffers->push_back(index_buffer);
  }
}

void PipelineDrawer::bindResourceSet(uint32 index, ResourceSet* resource_set) {
  if (_execute_type == RECORD) {
    auto handle = resource_set->get();
    vkCmdBindDescriptorSets(
      _cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, _layout, index, 1, &handle, 0, nullptr
    );
  } else {
    TOY_ASSERT(_dset_layouts[index] == resource_set->getLayout());
    _resource_sets->push_back(resource_set);
  }
}

void PipelineDrawer::setStencilReference(uint32 reference) {
  if (_execute_type == RECORD) {
    vkCmdSetStencilReference(_cmdbuf, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
  }
}

void PipelineDrawer::draw() {
  if (_execute_type == RECORD) {
    vkCmdDrawIndexed(_cmdbuf, _index_count, 1, 0, 0, 0);
  }
}

void recordPipeline(
  VkCommandBuffer      cmdbuf,
  VkExtent2D           extent,
  VkPipeline           pipeline,
  VkPipelineLayout     layout,
  PipelineDrawRecorder recorder
) {
  vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
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
  recorder(PipelineDrawer::forRecord(cmdbuf, pipeline, layout, extent));
}

RenderPassPipeline::RenderPassPipeline(
  std::span<AttachmentInfo const> attachments, std::span<SubpassPipelineInfo const> subpasses
)
  : _render_pass{
      attachments,
      subpasses | views::transform([](auto& x) {
        return SubpassInfo{
          .colors = x.colors,
          .multi_sample = x.multi_sample,
          .depst = x.depst.transform([](auto x) { return x.attachment; }),
          .inputs = x.inputs,
        };
      }) |
        ranges::to<std::vector>(),
    } {
  for (auto [subpass_i, subpass] : subpasses | toy::enumerate) {
    auto pipeline_info = PipelineInfo{
      .render_pass = _render_pass,
      .subpass_i = subpass_i,
      .vertex_shader_name = subpass.vertex_shader_name,
      .frag_shader_name = subpass.frag_shader_name,
      .dset_layouts = std::move(subpass.dset_layouts),
      .topology = subpass.topology,
      .cull_mode = subpass.cull_mode,
      .sample_count =
        subpass.multi_sample ? subpass.multi_sample->sample_count : VK_SAMPLE_COUNT_1_BIT,
      .output_n = static_cast<uint32_t>(subpass.colors.size()),
      .stencil_option = subpass.depst.transform([](auto x) { return x.stencil_option; }),
      .depth_option = subpass.depst.transform([](auto x) { return x.depth_option; }),
      .vertex_info = subpass.vertex_info,
    };
    _pipelines.push_back(Pipeline{ pipeline_info });
    _pipeline_dset_layouts.push_back(std::move(pipeline_info.dset_layouts));
  }
  _recorders.resize(subpasses.size());
}

void RenderPassPipeline::recordDraw(
  std::span<FrameImageManager*> images, std::vector<VkClearValue> clear_values
) {
  auto& executor = _render_pass.getExecutor();

  auto batches = std::vector<CommandBatch>{};
  auto waitables_keep_lifetime = std::list<Waitable>{};
  auto addSync = [&](SyncContext sync) {
    if (auto* ctx = std::get_if<BarrierRecorder>(&sync)) {
      batches.push_back(CommandBatch{ std::move(*ctx) });
    } else if (auto* ctx = std::get_if<FamilyTransferRecorder>(&sync)) {
      auto waitable = ctx->executeRelease();
      waitables_keep_lifetime.push_back(std::move(waitable));
      batches.push_back(ctx->toAcquireBatch(&waitables_keep_lifetime.back()));
    }
  };
  for (auto [layouts, recorder] : views::zip(_pipeline_dset_layouts, _recorders)) {
    auto vertex_buffers = std::vector<Buffer*>{};
    auto index_buffers = std::vector<Buffer*>{};
    auto resource_sets = std::vector<ResourceSet*>{};
    auto drawer =
      PipelineDrawer::forGetResources(layouts, &vertex_buffers, &index_buffers, &resource_sets);
    recorder(drawer);
    for (auto* vertex_buffer : vertex_buffers) {
      auto sync = vertex_buffer->getTracker().syncScope(
        Scope{
          .stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
          .access_mask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        },
        executor.getFamily()
      );
      addSync(std::move(sync));
    }
    for (auto* index_buffer : index_buffers) {
      auto sync = index_buffer->getTracker().syncScope(
        Scope{
          .stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
          .access_mask = VK_ACCESS_INDEX_READ_BIT,
        },
        executor.getFamily()
      );
      addSync(std::move(sync));
    }
    for (auto* resource_set : resource_sets) {
      for (auto resource : resource_set->getResources()) {
        auto ctx = resource.resource->getDescriptorContext();
        auto type = resource_set->getInfo()[resource.binding_i].type;
        auto shader_stage = resource_set->getInfo()[resource.binding_i].stage;
        using ImageContext = DescriptorResource::ImageContext;
        using BufferContext = DescriptorResource::BufferContext;
        if (auto* image_ctx = std::get_if<ImageContext>(&ctx)) {
          auto stage = [&]() -> VkPipelineStageFlags {
            switch (shader_stage) {
            case VK_SHADER_STAGE_FRAGMENT_BIT:
              return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            default:
              toy::throwf("unsupported shader stage");
            }
          }();
          auto layout = [&]() -> VkImageLayout {
            switch (type) {
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
              return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            default:
              toy::throwf("unsupported descriptor type");
            }
          }();
          auto access = [&]() -> VkAccessFlags {
            switch (type) {
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
              return VK_ACCESS_SHADER_READ_BIT;
            default:
              toy::throwf("unsupported descriptor type");
            }
          }();
          addSync(
            image_ctx->tracker->syncScope(Scope{ stage, access }, executor.getFamily(), layout)
          );
        } else if (auto* buffer_ctx = std::get_if<BufferContext>(&ctx)) {
          auto stage = [&]() -> VkPipelineStageFlags {
            switch (shader_stage) {
            case VK_SHADER_STAGE_FRAGMENT_BIT:
              return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            case VK_SHADER_STAGE_VERTEX_BIT:
              return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
            default:
              toy::throwf("unsupported shader stage");
            }
          }();
          auto access = [&]() -> VkAccessFlags {
            switch (type) {
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
              return VK_ACCESS_UNIFORM_READ_BIT;
            default:
              toy::throwf("unsupported descriptor type");
            }
          }();
          addSync(buffer_ctx->tracker->syncScope(Scope{ stage, access }, executor.getFamily()));
        }
      }
    }
  }
  auto pipeline_recorder = [&](VkCommandBuffer cmdbuf, VkExtent2D extent) {
    for (auto [subpass_i, pipeline, recorder] :
         views::zip(views::iota(0u), _pipelines, _recorders)) {
      if (subpass_i != 0) {
        vkCmdNextSubpass(cmdbuf, VK_SUBPASS_CONTENTS_INLINE);
      }
      recordPipeline(cmdbuf, extent, pipeline.getPipeline(), pipeline.getLayout(), recorder);
    }
  };
  _render_pass.record(batches, images, clear_values, pipeline_recorder);
}

} // namespace rd