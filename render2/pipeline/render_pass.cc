module render.render_pass2;

import <vulkan_config.h>;

import render.sync;
import render.executor;
import render.tracker;

namespace rd {

RenderPass::RenderPass(
  std::span<AttachmentInfo const> attachments, std::span<SubpassPipelineInfo const> subpasses
) {
  auto subpass_infos = std::vector<SubpassInfo>{};
  for (auto& subpass : subpasses) {
    subpass_infos.push_back(SubpassInfo{
      .colors = subpass.colors,
      .multi_sample = subpass.multi_sample,
      .depst = subpass.depst.transform([](auto x) { return x.attachment; }),
      .inputs = subpass.inputs,
    });
  }
  auto& render_pass = static_cast<rs::RenderPass&>(*this);
  std::tie(render_pass, _attachment_syncs) = createRenderPass(attachments, subpass_infos);

  for (auto [subpass_i, subpass] : subpasses | toy::enumerate) {
    auto vertex_shader = createShaderModule(subpass.vertex_shader_name);
    auto frag_shader = createShaderModule(subpass.frag_shader_name);
    auto pipeline_layout = createPipelineLayout(subpass.dset_layouts);
    auto pipeline_info = PipelineInfo{
      .render_pass = render_pass,
      .subpass_index = subpass_i,
      .vertex_shader = vertex_shader,
      .frag_shader = frag_shader,
      .layout = pipeline_layout,
      .topology = subpass.topology,
      .sample_count =
        subpass.multi_sample ? subpass.multi_sample->sample_count : VK_SAMPLE_COUNT_1_BIT,
      .stencil_option = subpass.depst.transform([](auto x) { return x.stencil_option; }),
      .depth_option = subpass.depst.transform([](auto x) { return x.depth_option; }),
      .vertex_bindings = std::array{ *subpass.vertex_info.binding_description },
      .vertex_attribs = subpass.vertex_info.attribute_descriptions,
    };
    auto pipeline = createGraphicsPipeline(pipeline_info);
    _pipelines.push_back(PipelineResource{
      .vertex_shader = std::move(vertex_shader),
      .frag_shader = std::move(frag_shader),
      .layout = std::move(pipeline_layout),
      .pipeline = std::move(pipeline),
    });
  }
}

auto PipelineDrawer::forRecord(
  VkCommandBuffer cmdbuf, VkPipeline pipeline, VkPipelineLayout layout, VkExtent2D extent
) -> PipelineDrawer {
  return PipelineDrawer{ cmdbuf, pipeline, layout, extent, RECORD };
}

auto PipelineDrawer::forGetResources(
  VkPipeline pipeline, VkPipelineLayout layout, VkExtent2D extent
) -> PipelineDrawer {
  return PipelineDrawer{ VK_NULL_HANDLE, pipeline, layout, extent, GET_RESOURCES };
}

void PipelineDrawer::bindVertexBuffer(VertexBuffer* vertex_buffer) {
  if (_execute_type == RECORD) {
    auto offset = VkDeviceSize{ 0 };
    auto buffer = vertex_buffer->get();
    vkCmdBindVertexBuffers(_cmdbuf, 0, 1, &buffer, &offset);
  } else {
    _vertex_buffers.push_back(vertex_buffer);
  }
}

void PipelineDrawer::bindIndexBuffer(IndexBuffer* index_buffer) {
  if (_execute_type == RECORD) {
    vkCmdBindIndexBuffer(_cmdbuf, *index_buffer, 0, index_buffer->getIndexType());
    _index_count = index_buffer->getIndexNumber();
  } else {
    _index_buffers.push_back(index_buffer);
  }
}

void PipelineDrawer::bindResourceSet(uint32 index, ResourceSet* resource_set) {
  if (_execute_type == RECORD) {
    auto handle = resource_set->get();
    vkCmdBindDescriptorSets(
      _cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, _layout, index, 1, &handle, 0, nullptr
    );
  } else {
    _resource_sets.push_back(resource_set);
  }
}

void PipelineDrawer::draw() {
  if (_execute_type == RECORD) {
    vkCmdDrawIndexed(_cmdbuf, _index_count, 1, 0, 0, 0);
  }
}

void recordRenderPassDraw(VkCommandBuffer cmdbuf, RenderPassDrawInfo info) {
  auto& [render_pass, framebuffer, clear_values, extent, pipeline_infos] = info;
  auto render_pass_begin_info = VkRenderPassBeginInfo{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = render_pass,
    .framebuffer = framebuffer,
    .renderArea = VkRect2D{ .offset = VkOffset2D{ 0, 0 }, .extent = extent },
    .clearValueCount = static_cast<uint32>(clear_values.size()),
    .pClearValues = clear_values.data(),
  };
  // VK_SUBPASS_CONTENTS_: render pass的command被嵌入主缓冲区
  // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS: render pass 命令
  // 将会从次缓冲区执行
  vkCmdBeginRenderPass(cmdbuf, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  for (auto& [pipeline, layout, recorder] : pipeline_infos) {
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
  vkCmdEndRenderPass(cmdbuf);
}

void RenderPassManager::recordDraw(
  std::span<FrameImageManager*> images, std::vector<VkClearValue> clear_values, VkExtent2D extent
) {
  auto  framebuffer = FramebufferPool::getInstance().getFramebuffer(get(), images);
  auto& executor = CommandExecutorManager::getInstance()[FamilyType::GRAPHICS];

  auto drawers = std::vector<PipelineDrawer>{};
  for (auto& info : getPipelines()) {
    drawers.push_back(PipelineDrawer::forGetResources(info.pipeline, info.layout, extent));
  }
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
  for (auto [image, info] : views::zip(images, getSyncInfos())) {
    addSync(image->getTracker().syncScope(
      Scope{ .stage_mask = info.initial_stage }, executor.getFamily(), info.initial_layout
    ));
  }
  for (auto [drawer, recorder] : views::zip(drawers, _recorders)) {
    recorder(drawer);
    for (auto* vertex_buffer : drawer.getVertexBuffers()) {
      auto sync = vertex_buffer->getTracker().syncScope(
        Scope{
          .stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
          .access_mask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        },
        executor.getFamily()
      );
      addSync(std::move(sync));
    }
    for (auto* index_buffer : drawer.getIndexBuffers()) {
      auto sync = index_buffer->getTracker().syncScope(
        Scope{
          .stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
          .access_mask = VK_ACCESS_INDEX_READ_BIT,
        },
        executor.getFamily()
      );
      addSync(std::move(sync));
    }
    for (auto* resource_set : drawer.getResourceSets()) {
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
  auto pipeline_infos = std::vector<RenderPassDrawInfo::PipelineDrawInfo>{};
  for (auto [resource, recorder] : views::zip(getPipelines(), _recorders)) {
    pipeline_infos.push_back(RenderPassDrawInfo::PipelineDrawInfo{
      .pipeline = resource.pipeline,
      .layout = resource.layout,
      .recorder = recorder,
    });
  }
  auto info = RenderPassDrawInfo{
    .render_pass = get(),
    .framebuffer = framebuffer,
    .clear_values = std::move(clear_values),
    .extent = extent,
    .pipeline_infos = std::move(pipeline_infos),
  };
  using namespace std::placeholders;
  batches.push_back(CommandBatch{ std::bind(recordRenderPassDraw, _1, info) });
  auto waitables = executor.submit(batches);
  auto waitable_ptr = std::make_shared<Waitable>(std::move(waitables.back()));
  for (auto [image, info] : views::zip(images, getSyncInfos())) {
    image->getTracker().setNewScope(
      Scope{ .stage_mask = info.final_stage }, executor.getFamily(), info.final_layout
    );
  }
}

} // namespace rd