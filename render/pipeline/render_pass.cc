module;
#include <toy.h>
module render.render_pass;

import <vulkan_config.h>;

import render.sync;
import render.executor;
import render.tracker;

namespace rd {

void recordRenderPass(
  VkCommandBuffer                                       cmdbuf,
  VkRenderPass                                          render_pass,
  VkFramebuffer                                         framebuffer,
  VkExtent2D                                            extent,
  std::span<VkClearValue const>                         clear_values,
  std::span<std::function<void(VkCommandBuffer)> const> recorders
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
  for (auto [subpass_i, recorder] : recorders | toy::enumerate) {
    if (subpass_i != 0) {
      vkCmdNextSubpass(cmdbuf, VK_SUBPASS_CONTENTS_INLINE);
    }
    recorder(cmdbuf);
  }
  vkCmdEndRenderPass(cmdbuf);
}

auto RenderPass::record(
  std::vector<CommandBatch>                                         batches,
  std::span<FrameImageManager* const>                               images,
  std::span<std::function<void(VkCommandBuffer, VkExtent2D)> const> pipeline_recorders
) -> Waitable {
  for (auto [image, format, sample] : views::zip(images, _formats, _sample_counts)) {
    TOY_ASSERT(image->getFormat() == format, image->getFormat(), format);
    TOY_ASSERT(image->getSampleCount() == sample, image->getSampleCount(), sample);
  }
  TOY_ASSERT(_need_clears == _is_set_clears, _need_clears, _is_set_clears);
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
      cmdbuf,
      get(),
      framebuffer,
      extent,
      _clear_values,
      pipeline_recorders | views::transform([&](auto& recorder) {
        return std::function<void(VkCommandBuffer)>{ [&](auto cmdbuf) {
          recorder(cmdbuf, extent);
        } };
      }) |
        ranges::to<std::vector>()
    );
  } });
  auto waitable = std::move(_executor->submit(batches).back());

  for (auto [image, info] : views::zip(images, getSyncInfos())) {
    image->getTracker().setNewScope(
      Scope{ .stage_mask = info.final_stage }, _executor->getFamily(), info.final_layout
    );
  }
  return waitable;
}

RenderPassPipeline::RenderPassPipeline(
  std::span<AttachmentInfo const> attachments, std::span<SubpassPipelineInfo const> subpasses
)
  : RenderPass{
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
    auto push_constant_ranges = getPushConstantRanges(subpass.push_constants);
    auto pipeline_info = PipelineInfo{
      .render_pass = get(),
      .subpass_i = subpass_i,
      .vertex_shader_name = subpass.vertex_shader_name,
      .frag_shader_name = subpass.frag_shader_name,
      .dset_layouts = std::move(subpass.dset_layouts),
      .push_constants = std::move(push_constant_ranges),
      .topology = subpass.topology,
      .cull_mode = subpass.cull_mode,
      .sample_count =
        subpass.multi_sample ? subpass.multi_sample->sample_count : VK_SAMPLE_COUNT_1_BIT,
      .output_n = static_cast<uint32_t>(subpass.colors.size()),
      .stencil_option = subpass.depst ? subpass.depst->stencil : std::nullopt,
      .depth_option = subpass.depst ? subpass.depst->depth : std::nullopt,
      .vertex_layout = subpass.vertex_layout,
    };
    if (subpass.depst) {
      auto format = attachments[subpass.depst->attachment].format;
      TOY_ASSERT(
        subpass.depst->depth.has_value() == bool(getFormatInfo(format).type & FormatType::DEPTH),
        format
      );
      TOY_ASSERT(
        subpass.depst->stencil.has_value() ==
          bool(getFormatInfo(format).type & FormatType::STENCIL),
        format
      );
    }
    _pipelines.push_back(Pipeline{ pipeline_info });
  }
  _recorders.resize(subpasses.size());
}

void RenderPassPipeline::recordDraw(std::span<FrameImageManager*> images) {
  auto& executor = getExecutor();

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
  auto total_dsets = std::vector<DescriptorSet*>{};
  for (auto recorder : _recorders) {
    auto vertex_buffers = std::vector<Buffer*>{};
    auto index_buffers = std::vector<Buffer*>{};
    auto dsets = std::vector<DescriptorSet*>{};
    auto drawer = PipelineDrawer::forCollect(&vertex_buffers, &index_buffers, &dsets);
    recorder(drawer);
    total_dsets.append_range(dsets);
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
    for (auto* dset : dsets) {
      for (auto resource : dset->getResources()) {
        auto ctx = resource.resource->getDescriptorContext();
        auto type = dset->getInfo()[resource.binding_i].type;
        auto shader_stage = dset->getInfo()[resource.binding_i].stage;
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
  auto pipeline_recorders = std::vector<std::function<void(VkCommandBuffer, VkExtent2D)>>{};
  for (auto [pipeline, recorder] : views::zip(_pipelines, _recorders)) {
    pipeline_recorders.push_back([&](VkCommandBuffer cmdbuf, VkExtent2D extent) {
      pipeline.record(cmdbuf, extent, recorder);
    });
  }
  auto waitable = std::make_shared<Waitable>(record(batches, images, pipeline_recorders));
  for (auto* dset : total_dsets) {
    dset->addWaitable(waitable);
  }
}

} // namespace rd