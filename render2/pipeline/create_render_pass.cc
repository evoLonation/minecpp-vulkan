module render.vk.render_pass;

import "vulkan_config.h";
import render.vk.resource;
import render.vk.device;
import render.vk.sync;
import render.vk.reflections;
import render.vertex;

import std;
import toy;

namespace rd::vk {

auto RenderPass::createRenderPass(
  std::span<const AttachmentInfo> attachments, std::span<const SubpassInfo> subpasses
) -> std::tuple<rs::RenderPass, std::vector<AttachmentSyncInfo>> {
  /**
   * @brief get attachment refs and subpass descriptions
   *
   */
  auto subpass_descriptions = std::vector<VkSubpassDescription2>{};
  auto attachment_refs = std::vector<VkAttachmentReference2>{};
  for (auto [subpass_i, subpass] : subpasses | toy::enumerate) {
    auto check = [subpass_i](bool condition, uint32 attachment_i, std::string_view msg) {
      if (!condition) {
        toy::throwf("Subpass {} is not match with attachment {}: {}", subpass_i, attachment_i, msg);
      }
    };
    auto color_index = static_cast<uint32>(attachment_refs.size());
    for (auto color_i : subpass.colors) {
      auto attachment = attachments[color_i];
      check(attachment.format.getType() == AttachmentFormat::COLOR, color_i, "not color format");
      check(
        subpass.multi_sample.has_value()
          ? attachment.sample_count == subpass.multi_sample->sample_count
          : attachment.sample_count == VK_SAMPLE_COUNT_1_BIT,
        color_i,
        "the sample count is not match"
      );
      attachment_refs.push_back(VkAttachmentReference2{
        .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
        // 引用的 attachment 的索引
        .attachment = color_i,
        // 用到该 ref 的 subpass 过程中使用的布局，会自动转换
        // if enable multi sample, resolve op also occur in color attachment ouput stage
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        // aspectMask is just used for input attachment
      });
    }
    auto resolve_index = static_cast<uint32>(attachment_refs.size());
    if (subpass.multi_sample.has_value()) {
      for (auto resolve_i_opt : subpass.multi_sample->resolves) {
        if (resolve_i_opt.has_value()) {
          auto  resolve_i = resolve_i_opt.value();
          auto& resolve_attachment = attachments[resolve_i];
          check(
            resolve_attachment.sample_count == VK_SAMPLE_COUNT_1_BIT &&
              resolve_attachment.format.getType() == AttachmentFormat::COLOR,
            resolve_i,
            "not capable for resolve attachment"
          );
          attachment_refs.push_back(VkAttachmentReference2{
            .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .attachment = resolve_i,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          });
        } else {
          attachment_refs.push_back(VkAttachmentReference2{
            .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
            .attachment = VK_ATTACHMENT_UNUSED,
            .layout = VK_IMAGE_LAYOUT_UNDEFINED,
          });
        }
      }
    }
    auto depst_index = std::optional<uint32>{};
    if (subpass.depst_info.has_value()) {
      auto depst_attachment_i = subpass.depst_info->attachment;
      depst_index = static_cast<uint32>(attachment_refs.size());
      attachment_refs.push_back(VkAttachmentReference2{
        .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
        .attachment = depst_attachment_i,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      });
      auto& attachment = attachments[depst_attachment_i];
      check(
        subpass.multi_sample.has_value()
          ? attachment.sample_count == subpass.multi_sample->sample_count
          : attachment.sample_count == VK_SAMPLE_COUNT_1_BIT,
        depst_attachment_i,
        "the sample count is not match"
      );
      check(
        attachment.format.getType() & AttachmentFormat::DEPTH_STENCIL,
        depst_attachment_i,
        "not depth or stencil"
      );
    }
    auto input_index = static_cast<uint32>(attachment_refs.size());
    for (auto input_i : subpass.inputs) {
      check(
        !ranges::contains(subpass.colors, input_i) &&
          (!subpass.depst_info.has_value() || input_i != subpass.depst_info->attachment),
        input_i,
        "input attachment write in same subpass"
      );
      if (attachments[input_i].format.getType() == AttachmentFormat::COLOR) {
        attachment_refs.push_back(VkAttachmentReference2{
          .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
          .attachment = input_i,
          .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        });
      } else {
        attachment_refs.push_back(VkAttachmentReference2{
          .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
          .attachment = input_i,
          .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
          .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        });
      }
    }
    subpass_descriptions.push_back(VkSubpassDescription2{
      .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
      // 还有 compute、 ray tracing 等等
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      // 这里的数组的索引和 着色器里的 layout 数值一一对应
      .inputAttachmentCount = static_cast<uint32>(subpass.inputs.size()),
      .pInputAttachments = attachment_refs.data() + input_index,
      .colorAttachmentCount = static_cast<uint32>(subpass.colors.size()),
      .pColorAttachments = attachment_refs.data() + color_index,
      .pResolveAttachments =
        subpass.multi_sample.has_value() ? attachment_refs.data() + resolve_index : nullptr,
      // pInputAttachments: Attachments that are read from a shader
      // pResolveAttachments: Attachments used for multisampling color attachments
      // pDepthStencilAttachment: Attachment for depth and stencil data
      .pDepthStencilAttachment =
        depst_index.transform([&](auto index) { return &attachment_refs[index]; }
        ).value_or(nullptr),
      // pPreserveAttachments: Attachments that are not used by this subpass, but
      // for which the data must be preserved
    });
  }
  /**
   * @brief get attachment descriptions
   *
   */
  auto attachment_initial_layout = std::vector<VkImageLayout>{};
  attachment_initial_layout.resize(attachments.size(), VK_IMAGE_LAYOUT_UNDEFINED);
  auto attachment_final_layout = std::vector<VkImageLayout>{};
  attachment_final_layout.resize(attachments.size(), VK_IMAGE_LAYOUT_UNDEFINED);
  for (auto& ref : attachment_refs | views::filter([](auto& attachment) {
                     return attachment.attachment != VK_ATTACHMENT_UNUSED;
                   })) {
    if (attachment_initial_layout[ref.attachment] == VK_IMAGE_LAYOUT_UNDEFINED) {
      attachment_initial_layout[ref.attachment] = ref.layout;
    }
    attachment_final_layout[ref.attachment] = ref.layout;
  }
  auto attachment_descs =
    attachments | toy::enumerate | views::transform([&](auto pair) {
      auto& [i, attachment] = pair;
      return VkAttachmentDescription2{
        .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2,
        /**
         * @brief if color format, the stencilxxxOp is ignored;
         * if depth and/or stencil format, xxxOp apply to depth, stencilxxxOp apply to stencil
         */
        .format = attachment.format,
        .samples = attachment.sample_count,
        /**
         * @brief load op: define load operation behavior of color and depth
         * the load op happen in VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT(color
         * attachment) or VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT(depth attachment) and
         * happen before any command which access the sample in the render pass
         */
        // VK_ATTACHMENT_LOAD_OP_LOAD: 保留 attachment 中现有内容
        // VK_ATTACHMENT_LOAD_OP_CLEAR: 将其中内容清理为一个常量
        // VK_ATTACHMENT_LOAD_OP_DONT_CARE: 不在乎
        .loadOp =
          attachment.keep_old_content ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
        /**
         * @brief store op: define store operation behavior of color and depth
         * the store op happen in VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT(color
         * attachment) or VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT(depth attachment) and
         * happen after any command which access the sample in the render pass
         */
        // VK_ATTACHMENT_STORE_OP_STORE: 渲染后内容存入内存稍后使用
        // VK_ATTACHMENT_STORE_OP_DONT_CARE: 不在乎
        .storeOp = attachment.keep_new_content ? VK_ATTACHMENT_STORE_OP_STORE
                                               : VK_ATTACHMENT_STORE_OP_DONT_CARE,

        .stencilLoadOp =
          attachment.keep_old_content ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = attachment.keep_new_content ? VK_ATTACHMENT_STORE_OP_STORE
                                                      : VK_ATTACHMENT_STORE_OP_DONT_CARE,
        // 开启及结束时 要求 的图像布局
        // UNDEINFED init layout use with CLEAR load op together
        .initialLayout = attachment_initial_layout[i],
        .finalLayout = attachment_final_layout[i],
      };
    }) |
    ranges::to<std::vector>();

  /**
   * @brief get subpass dependencies
   *
   */
  auto barriers = std::map<std::pair<uint32, uint32>, std::vector<VkMemoryBarrier2>>{};
  // key: attachment, value: {src_subpass, dst_subpass, barrier_index}[]
  auto attachment_barriers = std::vector<std::vector<std::tuple<uint32, uint32, uint32>>>{};
  attachment_barriers.resize(attachments.size());
  // Either there is one write, or there are multiple reads
  // For certine framebuf, the reads and write can contains subpass at the same time, the write is
  // before the reads
  // attachment_i -> subpass_i
  auto color_last_write = std::map<uint32, uint32>{};
  auto depst_last_write = std::map<uint32, uint32>{};
  // attachment_i -> vector of subpass_i
  auto last_read = std::map<uint32, std::vector<uint32>>{};

  auto color_scope = Scope{
    .stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
  };
  auto depst_scope = Scope{
    .stage_mask =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
    .access_mask =
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
  };
  auto input_scope = vk::Scope{
    .stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    .access_mask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
  };
  auto add_dependency_ =
    [&](
      uint32 src_subpass, uint32 dst_subpass, Scope src_scope, Scope dst_scope, uint32 attachment
    ) {
      auto& barriers_ = barriers[{ src_subpass, dst_subpass }];
      barriers_.push_back(VkMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = static_cast<VkPipelineStageFlags>(src_scope.stage_mask),
        .srcAccessMask = static_cast<VkAccessFlags>(src_scope.access_mask),
        .dstStageMask = static_cast<VkPipelineStageFlags>(dst_scope.stage_mask),
        .dstAccessMask = static_cast<VkAccessFlags>(dst_scope.access_mask),
      });
      attachment_barriers[attachment].emplace_back(src_subpass, dst_subpass, barriers_.size() - 1);
    };
  for (auto [subpass_i, subpass] : subpasses | toy::enumerate) {
    for (auto color_i : subpass.colors) {
      auto add_dependency = [&](uint32 src_subpass, vk::Scope src_scope) {
        add_dependency_(src_subpass, subpass_i, src_scope, color_scope, color_i);
      };
      if (last_read.contains(color_i)) {
        for (auto read : last_read.at(color_i)) {
          add_dependency(read, input_scope.extractWriteAccess());
        }
        last_read.erase(color_i);
      } else if (color_last_write.contains(color_i)) {
        add_dependency(color_last_write.at(color_i), color_scope.extractWriteAccess());
      } else {
        add_dependency(VK_SUBPASS_EXTERNAL, Scope{ .stage_mask = color_scope.stage_mask });
      }
      color_last_write[color_i] = subpass_i;
    }
    if (subpass.multi_sample.has_value()) {
      for (auto resolve_opt : subpass.multi_sample->resolves) {
        if (!resolve_opt.has_value()) {
          continue;
        }
        auto resolve_i = resolve_opt.value();
        auto add_dependency = [&](uint32 src_subpass, vk::Scope src_scope) {
          add_dependency_(src_subpass, subpass_i, src_scope, color_scope, resolve_i);
        };
        if (last_read.contains(resolve_i)) {
          for (auto read : last_read.at(resolve_i)) {
            add_dependency(read, input_scope.extractWriteAccess());
          }
          last_read.erase(resolve_i);
        } else if (color_last_write.contains(resolve_i)) {
          add_dependency(color_last_write.at(resolve_i), color_scope.extractWriteAccess());
        } else {
          add_dependency(VK_SUBPASS_EXTERNAL, Scope{ .stage_mask = color_scope.stage_mask });
        }
        color_last_write[resolve_i] = subpass_i;
      }
    }
    if (subpass.depst_info.has_value()) {
      auto depst_i = subpass.depst_info->attachment;
      auto add_dependency = [&](uint32 src_subpass, vk::Scope src_scope) {
        add_dependency_(src_subpass, subpass_i, src_scope, depst_scope, depst_i);
      };
      if (last_read.contains(depst_i)) {
        for (auto read : last_read.at(depst_i)) {
          add_dependency(read, input_scope.extractWriteAccess());
        }
        last_read.erase(depst_i);
      } else if (depst_last_write.contains(depst_i)) {
        add_dependency(depst_last_write.at(depst_i), depst_scope.extractWriteAccess());
      } else {
        add_dependency(VK_SUBPASS_EXTERNAL, Scope{ .stage_mask = depst_scope.stage_mask });
      }
      depst_last_write[depst_i] = subpass_i;
    }
    for (auto& input_i : subpass.inputs) {
      auto add_dependency = [&](uint32 src_subpass, vk::Scope src_scope) {
        add_dependency_(src_subpass, subpass_i, src_scope, input_scope, input_i);
      };
      if (color_last_write.contains(input_i)) {
        add_dependency(color_last_write.at(input_i), color_scope.extractWriteAccess());
      } else if (depst_last_write.contains(input_i)) {
        add_dependency(depst_last_write.at(input_i), depst_scope.extractWriteAccess());
      } else {
        toy::throwf("the input attachment appears byfore a write");
      }
      last_read[input_i].push_back(subpass_i);
    }
  }
  for (auto [attachment_i, subpass_i] : color_last_write) {
    if (last_read.contains(attachment_i)) {
      continue;
    }
    add_dependency_(
      subpass_i,
      VK_SUBPASS_EXTERNAL,
      color_scope.extractWriteAccess(),
      Scope{ .stage_mask = color_scope.stage_mask },
      attachment_i
    );
  }
  for (auto [attachment_i, subpass_i] : depst_last_write) {
    if (last_read.contains(attachment_i)) {
      continue;
    }
    add_dependency_(
      subpass_i,
      VK_SUBPASS_EXTERNAL,
      depst_scope.extractWriteAccess(),
      Scope{ .stage_mask = depst_scope.stage_mask },
      attachment_i
    );
  }
  for (auto [attachment_i, subpasses] : last_read) {
    for (auto subpass_i : subpasses) {
      add_dependency_(
        subpass_i,
        VK_SUBPASS_EXTERNAL,
        input_scope.extractWriteAccess(),
        Scope{ .stage_mask = input_scope.stage_mask },
        attachment_i
      );
    }
  }
  auto merged_barriers = //
    barriers | views::transform([](auto& pair) {
      auto& [subpass_info, barrier] = pair;
      auto [src_scope, dst_scope] = std::reduce(
        barrier.begin(),
        barrier.end(),
        std::pair{ Scope{}, Scope{} },
        [](auto a, auto& b) {
          return std::pair{
            Scope{
              .stage_mask = a.first.stage_mask | b.srcStageMask,
              .access_mask = a.first.access_mask | b.srcAccessMask,
            },
            Scope{
              .stage_mask = a.second.stage_mask | b.dstStageMask,
              .access_mask = a.second.access_mask | b.dstAccessMask,
            },
          };
        }
      );
      auto memory_barrier = VkMemoryBarrier2{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = src_scope.stage_mask,
        .srcAccessMask = src_scope.access_mask,
        .dstStageMask = dst_scope.stage_mask,
        .dstAccessMask = dst_scope.access_mask,
      };
      return std::pair{ subpass_info, memory_barrier };
    }) |
    ranges::to<std::vector>();
  auto merged_dependencies = //
    merged_barriers | views::transform([](auto& pair) {
      auto& [subpass_info, memory_barrier] = pair;
      return VkSubpassDependency2{
        .sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2,
        .pNext = &memory_barrier,
        .srcSubpass = subpass_info.first,
        .dstSubpass = subpass_info.second,
      };
    }) |
    ranges::to<std::vector>();
  auto render_pass_create_info = VkRenderPassCreateInfo2{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
    .attachmentCount = static_cast<uint32>(attachment_descs.size()),
    .pAttachments = attachment_descs.data(),
    .subpassCount = static_cast<uint32>(subpass_descriptions.size()),
    .pSubpasses = subpass_descriptions.data(),
    .dependencyCount = static_cast<uint32>(merged_dependencies.size()),
    .pDependencies = merged_dependencies.data(),
  };
  auto render_pass = rs::RenderPass{ render_pass_create_info };
  auto attachment_sync_infos = std::vector<AttachmentSyncInfo>{};
  for (auto index : views::iota(0u, attachments.size())) {

    auto& initial_tuple = *ranges::find_if(attachment_barriers[index], [](auto& tuple) {
      return std::get<0>(tuple) == VK_SUBPASS_EXTERNAL;
    });
    auto  initial_stage = barriers[{ std::get<0>(initial_tuple), std::get<1>(initial_tuple) }]
                                 [std::get<2>(initial_tuple)]
                                   .srcStageMask;
    auto& final_tuple = *ranges::find_if(attachment_barriers[index], [](auto& tuple) {
      return std::get<1>(tuple) == VK_SUBPASS_EXTERNAL;
    });
    auto  final_stage = barriers[{ std::get<0>(initial_tuple), std::get<1>(initial_tuple) }]
                               [std::get<2>(initial_tuple)]
                                 .dstStageMask;
    attachment_sync_infos.push_back(AttachmentSyncInfo{
      .initial_stage = initial_stage,
      .final_stage = final_stage,
      .initial_layout = attachment_descs[index].initialLayout,
      .final_layout = attachment_descs[index].finalLayout,
    });
  }
  toy::debugf("the attachment sync infos: ");
  for (auto& info : attachment_sync_infos) {
    toy::debugf(
      "initial stage: {}, final stage: {}, initial layout: {}, final layout: {}",
      stageMask2Str(info.initial_stage),
      stageMask2Str(info.final_stage),
      refl::imageLayout(info.initial_layout),
      refl::imageLayout(info.final_layout)
    );
  }
  return { std::move(render_pass), std::move(attachment_sync_infos) };
}

} // namespace rd::vk