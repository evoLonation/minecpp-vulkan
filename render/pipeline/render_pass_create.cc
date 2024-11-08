module;
#include <toy.h>
module render.render_pass;

import <vulkan_config.h>;

import render.reflections;
import render.sync;
import render.executor;

namespace rd {

void checkSubpassAttachmentMatch(
  std::span<AttachmentInfo const> attachments, std::span<SubpassInfo const> subpasses
) {
  for (auto& subpass : subpasses) {
    auto sample_count =
      subpass.multi_sample ? subpass.multi_sample->sample_count : VK_SAMPLE_COUNT_1_BIT;

    auto current_outputs = std::vector<uint32>{};
    current_outputs.append_range(subpass.colors);
    // check colors
    auto colors = subpass.colors | views::transform([&](auto x) { return attachments[x]; }) |
                  ranges::to<std::vector>();
    TOY_ASSERT(
      ranges::all_of(colors, [&](auto x) { return x.sample_count == sample_count; }),
      colors | views::transform([](auto x) { return static_cast<uint32>(x.sample_count); })
    );
    TOY_ASSERT(ranges::all_of(colors, [&](auto x) {
      return x.format.getType() == AttachmentFormat::COLOR;
    }));

    // check resolves
    if (subpass.multi_sample) {
      TOY_ASSERT(subpass.multi_sample->sample_count != VK_SAMPLE_COUNT_1_BIT);
      TOY_ASSERT(subpass.multi_sample->resolves.size() == subpass.colors.size());
      auto resolve_indices =
        subpass.multi_sample->resolves | views::filter([](auto& x) { return x.has_value(); }) |
        views::transform([](auto x) { return x.value(); }) | ranges::to<std::vector>();
      current_outputs.append_range(resolve_indices);
      auto resolves = resolve_indices | views::transform([&](auto x) { return attachments[x]; }) |
                      ranges::to<std::vector>();
      TOY_ASSERT(
        ranges::all_of(resolves, [&](auto x) { return x.sample_count == VK_SAMPLE_COUNT_1_BIT; }),
        resolves | views::transform([](auto x) { return static_cast<uint32>(x.sample_count); })
      );
      TOY_ASSERT(ranges::all_of(resolves, [&](auto x) {
        return x.format.getType() == AttachmentFormat::COLOR;
      }));
    }
    // check depst
    if (subpass.depst) {
      current_outputs.push_back(subpass.depst.value());
      auto depst = attachments[subpass.depst.value()];
      TOY_ASSERT(depst.sample_count == sample_count);
      TOY_ASSERT(depst.format.getType() & AttachmentFormat::DEPTH_STENCIL);
    }
    // check inputs
    auto inputs = subpass.inputs | views::transform([&](auto x) { return attachments[x]; }) |
                  ranges::to<std::vector>();
    TOY_ASSERT(ranges::all_of(subpass.inputs, [&](auto i) {
      return !ranges::contains(current_outputs, i);
    }));
  }
}

struct SubpassAttachmentInfo {
  std::vector<std::vector<VkAttachmentReference2>> attachment_refs;
  std::vector<VkSubpassDescription2>               subpass_descs;
  // for each attachments
  std::vector<VkImageLayout> initial_layouts;
  std::vector<VkImageLayout> final_layouts;
};

auto createSubpassDescriptions(
  std::span<AttachmentInfo const> attachments, std::span<SubpassInfo const> subpasses
) -> SubpassAttachmentInfo {
  // each subpass corresponds to a std::vector<VkAttachmentReference2>
  auto attachment_refs_list = std::vector<std::vector<VkAttachmentReference2>>{};
  attachment_refs_list.resize(subpasses.size());
  auto initial_layouts = std::vector<VkImageLayout>{};
  initial_layouts.resize(attachments.size(), VK_IMAGE_LAYOUT_UNDEFINED);
  auto final_layouts = std::vector<VkImageLayout>{};
  final_layouts.resize(attachments.size(), VK_IMAGE_LAYOUT_UNDEFINED);

  auto now_attachment_refs = (std::vector<VkAttachmentReference2>*){};
  auto addAttachmentRef =
    [&](uint32 attach_i, VkImageLayout layout, VkImageAspectFlags aspect = {}) {
      now_attachment_refs->push_back(VkAttachmentReference2{
        .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
        // 引用的 attachment 的索引
        .attachment = attach_i,
        // 用到该 ref 的 subpass 过程中使用的布局，会自动转换
        // if enable multi sample, resolve op also occur in color attachment ouput stage
        .layout = layout,
        // aspectMask is just used for input attachment
        .aspectMask = aspect,
      });
      if (attach_i != VK_ATTACHMENT_UNUSED) {
        if (initial_layouts[attach_i] == VK_IMAGE_LAYOUT_UNDEFINED) {
          initial_layouts[attach_i] = layout;
        }
        final_layouts[attach_i] = layout;
      }
    };

  auto subpass_descriptions = std::vector<VkSubpassDescription2>{};
  for (auto [subpass, attachment_refs] : views::zip(subpasses, attachment_refs_list)) {
    now_attachment_refs = &attachment_refs;
    auto color_index = static_cast<uint32>(attachment_refs.size());
    for (auto color_i : subpass.colors) {
      addAttachmentRef(color_i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    auto resolve_index = static_cast<uint32>(attachment_refs.size());
    if (subpass.multi_sample) {
      for (auto resolve_i_ : subpass.multi_sample->resolves) {
        if (resolve_i_) {
          addAttachmentRef(resolve_i_.value(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        } else {
          addAttachmentRef(VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED);
        }
      }
    }
    auto depst_index = std::optional<uint32>{};
    if (subpass.depst) {
      depst_index = static_cast<uint32>(attachment_refs.size());
      addAttachmentRef(subpass.depst.value(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
    auto input_index = static_cast<uint32>(attachment_refs.size());
    for (auto input_i : subpass.inputs) {
      if (attachments[input_i].format.getType() == AttachmentFormat::COLOR) {
        addAttachmentRef(
          input_i, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT
        );
      } else {
        addAttachmentRef(
          input_i,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
          VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
        );
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
        subpass.multi_sample ? attachment_refs.data() + resolve_index : nullptr,
      // pInputAttachments: Attachments that are read from a shader
      // pResolveAttachments: Attachments used for multisampling color attachments
      // pDepthStencilAttachment: Attachment for depth and stencil data
      .pDepthStencilAttachment =
        depst_index.transform([&](auto i) { return &attachment_refs[i]; }).value_or(nullptr),
      // pPreserveAttachments: Attachments that are not used by this subpass, but
      // for which the data must be preserved
    });
  }
  return SubpassAttachmentInfo{
    std::move(attachment_refs_list),
    std::move(subpass_descriptions),
    std::move(initial_layouts),
    std::move(final_layouts),
  };
}

auto createAttachmentDescriptions(
  std::span<AttachmentInfo const> attachments,
  std::span<VkImageLayout const>  initial_layouts,
  std::span<VkImageLayout const>  final_layouts
) -> std::vector<VkAttachmentDescription2> {
  auto attachment_descs = std::vector<VkAttachmentDescription2>{};
  for (auto [i, attachment] : attachments | toy::enumerate) {
    auto desc = VkAttachmentDescription2{
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
      .initialLayout = initial_layouts[i],
      .finalLayout = final_layouts[i],
    };
    attachment_descs.push_back(desc);
  }
  return attachment_descs;
}

struct DependencyInfo {
  std::vector<VkMemoryBarrier2>     barriers;
  std::vector<VkSubpassDependency2> dependencies;
  // for each attachments
  std::vector<VkPipelineStageFlags2> initial_stages;
  std::vector<VkPipelineStageFlags2> final_stages;
};

auto createDependencies(
  std::span<const AttachmentInfo> attachments, std::span<const SubpassInfo> subpasses
) -> DependencyInfo {
  // key: {src_subpass, dst_subpass}, value: barrier
  auto barriers = std::map<std::pair<uint32, uint32>, VkMemoryBarrier2>{};
  // key: attachment, value: {src_subpass, dst_subpass, barrier_index}[]
  auto attachment_initial_stages = std::map<uint32, VkPipelineStageFlags2>{};
  auto attachment_final_stages = std::map<uint32, VkPipelineStageFlags2>{};
  auto add_dependency_ =
    [&](
      uint32 src_subpass, uint32 dst_subpass, Scope src_scope, Scope dst_scope, uint32 attachment
    ) {
      if (barriers.contains({ src_subpass, dst_subpass })) {
        auto& barrier = barriers[{ src_subpass, dst_subpass }];
        barrier.srcStageMask |= src_scope.stage_mask;
        barrier.srcAccessMask |= src_scope.access_mask;
        barrier.dstStageMask |= dst_scope.stage_mask;
        barrier.dstAccessMask |= dst_scope.access_mask;
      } else {
        barriers[{ src_subpass, dst_subpass }] = VkMemoryBarrier2{
          .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
          .srcStageMask = src_scope.stage_mask,
          .srcAccessMask = src_scope.access_mask,
          .dstStageMask = dst_scope.stage_mask,
          .dstAccessMask = dst_scope.access_mask,
        };
      }
      if (src_subpass == VK_SUBPASS_EXTERNAL) {
        TOY_ASSERT(
          !attachment_initial_stages.contains(attachment), attachment_initial_stages[attachment]
        );
        attachment_initial_stages[attachment] = src_scope.stage_mask;
      }
      if (dst_subpass == VK_SUBPASS_EXTERNAL) {
        TOY_ASSERT(
          !attachment_final_stages.contains(attachment), attachment_final_stages[attachment]
        );
        attachment_final_stages[attachment] = dst_scope.stage_mask;
      }
    };
  // Either there is one write, or there are multiple reads
  // For certine framebuf, the reads and write can contains subpass at the same time, the write
  // is before the reads
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
  auto input_scope = Scope{
    .stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
    .access_mask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT,
  };
  for (auto [subpass_i, subpass] : subpasses | toy::enumerate) {
    auto dealWriteAttachment = [&](bool is_color, uint32 attach_i, Scope dst_scope) {
      auto add_dependency = [&](uint32 src_subpass, Scope src_scope) {
        add_dependency_(src_subpass, subpass_i, src_scope, dst_scope, attach_i);
      };
      auto& last_write = is_color ? color_last_write : depst_last_write;
      if (last_read.contains(attach_i)) {
        for (auto read : last_read.at(attach_i)) {
          add_dependency(read, input_scope.extractWriteAccess());
        }
        last_read.erase(attach_i);
      } else {
        auto src_scope = is_color ? color_scope : depst_scope;
        if (last_write.contains(attach_i)) {
          add_dependency(last_write.at(attach_i), src_scope.extractWriteAccess());
        } else {
          add_dependency(VK_SUBPASS_EXTERNAL, Scope{ .stage_mask = src_scope.stage_mask });
        }
      }
      last_write[attach_i] = subpass_i;
    };
    auto dealReadAttachment = [&](uint32 attach_i, Scope dst_scope) {
      auto add_dependency = [&](uint32 src_subpass, Scope src_scope) {
        add_dependency_(src_subpass, subpass_i, src_scope, dst_scope, attach_i);
      };
      if (color_last_write.contains(attach_i)) {
        add_dependency(color_last_write.at(attach_i), color_scope.extractWriteAccess());
      } else if (depst_last_write.contains(attach_i)) {
        add_dependency(depst_last_write.at(attach_i), depst_scope.extractWriteAccess());
      } else {
        toy::throwf("the read(input) attachment appears byfore a write");
      }
      last_read[attach_i].push_back(subpass_i);
    };
    for (auto color_i : subpass.colors) {
      dealWriteAttachment(true, color_i, color_scope);
    }
    if (subpass.multi_sample) {
      for (auto resolve_opt : subpass.multi_sample->resolves) {
        if (resolve_opt) {
          dealWriteAttachment(true, resolve_opt.value(), color_scope);
        }
      }
    }
    if (subpass.depst) {
      dealWriteAttachment(false, subpass.depst.value(), depst_scope);
    }
    for (auto& input_i : subpass.inputs) {
      dealReadAttachment(input_i, input_scope);
    }
  }
  for (auto [attach_i, subpass_i] : color_last_write) {
    if (last_read.contains(attach_i)) {
      continue;
    }
    add_dependency_(
      subpass_i,
      VK_SUBPASS_EXTERNAL,
      color_scope.extractWriteAccess(),
      Scope{ .stage_mask = color_scope.stage_mask },
      attach_i
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
  auto barrier_list = barriers | views::values | ranges::to<std::vector>();
  auto dependencies = std::vector<VkSubpassDependency2>{};
  for (auto [subpass_info, memory_barrier] : views::zip(barriers | views::keys, barrier_list)) {
    dependencies.emplace_back(VkSubpassDependency2{
      .sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2,
      .pNext = &memory_barrier,
      .srcSubpass = subpass_info.first,
      .dstSubpass = subpass_info.second,
    });
  }

  TOY_ASSERT(ranges::all_of(views::iota(0u, static_cast<uint32>(attachments.size())), [&](auto i) {
    return attachment_initial_stages.contains(i) && attachment_final_stages.contains(i);
  }));
  auto initial_stages = attachment_initial_stages | views::values | ranges::to<std::vector>();
  auto final_stages = attachment_final_stages | views::values | ranges::to<std::vector>();
  auto attachment_syncs = std::vector<AttachmentSyncInfo>{};
  return DependencyInfo{
    std::move(barrier_list),
    std::move(dependencies),
    std::move(initial_stages),
    std::move(final_stages),
  };
}

RenderPass::RenderPass(
  std::span<AttachmentInfo const> attachments, std::span<SubpassInfo const> subpasses
) {
  checkSubpassAttachmentMatch(attachments, subpasses);
  auto [_1, subpass_descs, initial_layouts, final_layouts] =
    createSubpassDescriptions(attachments, subpasses);
  auto attach_descs = createAttachmentDescriptions(attachments, initial_layouts, final_layouts);
  auto [_2, dependencies, initial_stages, final_stages] =
    createDependencies(attachments, subpasses);
  // for (auto& desc : attach_descs) {
  //   TOY_DEBUG(
  //     desc.format,
  //     desc.samples,
  //     (int)desc.loadOp,
  //     (int)desc.storeOp,
  //     (int)desc.stencilLoadOp,
  //     (int)desc.stencilStoreOp,
  //     desc.initialLayout,
  //     desc.finalLayout
  //   );
  // }
  // for (auto& desc : subpass_descs) {
  //   auto& ref = *desc.pDepthStencilAttachment;
  //   TOY_DEBUG(ref.attachment, (int)ref.aspectMask, ref.layout);
  // }
  // for (auto& dep : dependencies) {
  //   TOY_DEBUG(
  //     dep.srcSubpass,
  //     dep.dstSubpass,
  //     (int)dep.srcStageMask,
  //     (int)dep.dstStageMask,
  //     (int)dep.srcAccessMask,
  //     (int)dep.dstAccessMask
  //   );
  // }
  auto render_pass_create_info = VkRenderPassCreateInfo2{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
    .attachmentCount = static_cast<uint32>(attach_descs.size()),
    .pAttachments = attach_descs.data(),
    .subpassCount = static_cast<uint32>(subpass_descs.size()),
    .pSubpasses = subpass_descs.data(),
    .dependencyCount = static_cast<uint32>(dependencies.size()),
    .pDependencies = dependencies.data(),
  };
  auto attachment_sync_infos = std::vector<AttachmentSyncInfo>{};
  for (auto [l1, l2, s1, s2] :
       views::zip(initial_layouts, final_layouts, initial_stages, final_stages)) {
    auto info = AttachmentSyncInfo{
      .initial_stage = s1,
      .final_stage = s2,
      .initial_layout = l1,
      .final_layout = l2,
    };
    attachment_sync_infos.push_back(info);
    TOY_DEBUG(
      stageMask2Str(info.initial_stage),
      stageMask2Str(info.final_stage),
      refl::imageLayout(info.initial_layout),
      refl::imageLayout(info.final_layout)
    );
  }
  rs::RenderPass::operator=({ render_pass_create_info });
  _attachment_syncs = std::move(attachment_sync_infos);
  _executor = &ExecutorManager::getInstance()[FamilyType::GRAPHICS];
}

} // namespace rd
