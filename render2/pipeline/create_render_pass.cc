module render.vk.render_pass;

import "vulkan_config.h";
import render.vk.resource;
import render.vk.device;
import render.vk.sync;
import render.vertex;

import std;
import toy;

namespace rd::vk {

auto RenderPass::createRenderPass(
  std::span<const AttachmentInfo> attachments, std::span<const SubpassInfo> subpasses
) -> rs::RenderPass {
  auto attachment_descs =
    attachments | views::transform([&](const AttachmentInfo& attachment) {
      toy::throwf(
        ranges::find(_color_formats, attachment.format) != _color_formats.end() ||
          ranges::find(_depth_formats, attachment.format) != _depth_formats.end() ||
          ranges::find(_stencil_formats, attachment.format) != _stencil_formats.end() ||
          ranges::find(_depth_stencil_formats, attachment.format) != _depth_stencil_formats.end(),
        "the format of attachment not support by renderpass"
      );
      return VkAttachmentDescription{
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
        .loadOp = attachment.enter_dep.keep_content ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                    : VK_ATTACHMENT_LOAD_OP_CLEAR,
        /**
         * @brief store op: define store operation behavior of color and depth
         * the store op happen in VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT(color
         * attachment) or VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT(depth attachment) and
         * happen after any command which access the sample in the render pass
         */
        // VK_ATTACHMENT_STORE_OP_STORE: 渲染后内容存入内存稍后使用
        // VK_ATTACHMENT_STORE_OP_DONT_CARE: 不在乎
        .storeOp = attachment.exit_dep.keep_content ? VK_ATTACHMENT_STORE_OP_STORE
                                                    : VK_ATTACHMENT_STORE_OP_DONT_CARE,

        .stencilLoadOp = attachment.enter_dep.keep_content ? VK_ATTACHMENT_LOAD_OP_LOAD
                                                           : VK_ATTACHMENT_LOAD_OP_CLEAR,
        .stencilStoreOp = attachment.exit_dep.keep_content ? VK_ATTACHMENT_STORE_OP_STORE
                                                           : VK_ATTACHMENT_STORE_OP_DONT_CARE,
        // 开启及结束时 要求 的图像布局
        // UNDEINFED init layout use with CLEAR load op together
        .initialLayout = attachment.enter_dep.layout,
        .finalLayout = attachment.exit_dep.layout,
      };
    }) |
    ranges::to<std::vector>();

  /**
   * @brief get subpass descriptions
   *
   */
  auto subpass_descriptions = std::vector<VkSubpassDescription>{};
  auto attachment_refs = std::vector<VkAttachmentReference>{};
  for (auto& subpass : subpasses) {
    auto color_index = static_cast<uint32_t>(attachment_refs.size());
    for (auto color_i : subpass.colors) {
      auto attachment = attachments[color_i];
      // check the format is match with attachment type (color or depst)
      toy::throwf(
        ranges::find(_color_formats, attachments[color_i].format) != _color_formats.end(),
        "the format of attachment[{}] is not color format",
        color_i
      );
      toy::throwf(
        subpass.multi_sample.has_value()
          ? attachment.sample_count == subpass.multi_sample->sample_count
          : attachment.sample_count == VK_SAMPLE_COUNT_1_BIT,
        "the sample count of color attachment[{}] is not match with subpass",
        color_i
      );
      attachment_refs.push_back(VkAttachmentReference{
        // 引用的 attachment 的索引
        .attachment = color_i,
        // 用到该 ref 的 subpass 过程中使用的布局，会自动转换
        // if enable multi sample, resolve op also occur in color attachment ouput stage
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      });
    }
    auto resolve_index = static_cast<uint32_t>(attachment_refs.size());
    if (subpass.multi_sample.has_value()) {
      for (auto resolve_i_opt : subpass.multi_sample->resolves) {
        if (resolve_i_opt.has_value()) {
          auto  resolve_i = resolve_i_opt.value();
          auto& resolve_attachment = attachments[resolve_i];
          toy::throwf(
            resolve_attachment.sample_count == VK_SAMPLE_COUNT_1_BIT &&
              ranges::find(_color_formats, resolve_attachment.format) != _color_formats.end(),
            "the attachment [{}] is not capable for resolve attachment",
            resolve_i
          );
          attachment_refs.push_back(VkAttachmentReference{
            .attachment = resolve_i,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          });
        } else {
          attachment_refs.push_back(VkAttachmentReference{
            .attachment = VK_ATTACHMENT_UNUSED,
            .layout = VK_IMAGE_LAYOUT_UNDEFINED,
          });
        }
      }
    }
    auto depst_index = std::optional<uint32_t>{};
    if (subpass.depst_attachment.has_value()) {
      auto depst_attachment_i = subpass.depst_attachment.value();
      depst_index = static_cast<uint32_t>(attachment_refs.size());
      attachment_refs.push_back(VkAttachmentReference{
        .attachment = depst_attachment_i,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      });
      auto& attachment = attachments[depst_attachment_i];
      toy::throwf(
        subpass.multi_sample.has_value()
          ? attachment.sample_count == subpass.multi_sample->sample_count
          : attachment.sample_count == VK_SAMPLE_COUNT_1_BIT,
        "the sample count of depth attachment[{}] is not match with subpass",
        depst_attachment_i
      );
      if (ranges::find(_depth_formats, attachment.format) != _depth_formats.end() || 
          ranges::find(_depth_stencil_formats, attachment.format) != _depth_stencil_formats.end()) {
        toy::throwf(
          subpass.depth_option.has_value(),
          "the attachment[{}] has depth component but depth_option is nullopt",
          depst_attachment_i
        );
      } else if (ranges::find(_stencil_formats, attachment.format) != _stencil_formats.end() || 
                 ranges::find(_depth_stencil_formats, attachment.format) != _depth_stencil_formats.end()) {
        toy::throwf(
          subpass.stencil_option.has_value(),
          "the attachment[{}] has stencil component but depth_option is nullopt",
          depst_attachment_i
        );
      }
    }
    auto input_index = static_cast<uint32_t>(attachment_refs.size());
    for (auto input_i : subpass.inputs) {
      toy::throwf(
        ranges::find(subpass.colors, input_i) == subpass.colors.end() &&
          (!subpass.depst_attachment.has_value() || input_i != subpass.depst_attachment.value()),
        "the input[{}] use attachment write in current subpass",
        input_i
      );
      if (ranges::find(_color_formats, attachments[input_i].format) != _color_formats.end()) {
        attachment_refs.push_back(VkAttachmentReference{
          .attachment = input_i,
          .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        });
      } else {
        attachment_refs.push_back(VkAttachmentReference{
          .attachment = input_i,
          .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        });
      }
    }
    subpass_descriptions.push_back(VkSubpassDescription{
      // 还有 compute、 ray tracing 等等
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      // 这里的数组的索引和 着色器里的 layout 数值一一对应
      .inputAttachmentCount = static_cast<uint32_t>(subpass.inputs.size()),
      .pInputAttachments = attachment_refs.data() + input_index,
      .colorAttachmentCount = static_cast<uint32_t>(subpass.colors.size()),
      .pColorAttachments = attachment_refs.data() + color_index,
      .pResolveAttachments = attachment_refs.data() + resolve_index,
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
   * @brief get subpass dependencies
   *
   */
  auto dependencies = std::map<std::pair<uint32_t, uint32_t>, std::vector<VkSubpassDependency>>{};
  // Either there is one write, or there are multiple reads
  // For certine frambuf, the reads and write can contains subpass at the same time, the write is
  // before the reads
  // attachment_i -> subpass_i
  auto color_last_write = std::map<uint32_t, uint32_t>{};
  auto depst_last_write = std::map<uint32_t, uint32_t>{};
  // attachment_i -> vector of subpass_i
  auto last_read = std::map<uint32_t, std::vector<uint32_t>>{};

  auto color_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  auto color_src_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  auto color_dst_access =
    VkAccessFlags{ VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
  auto color_src_scope = vk::Scope{ color_stage, color_src_access };
  auto depst_stage = VkPipelineStageFlags{ VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT };
  auto depst_src_access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  auto depst_dst_access = VkAccessFlags{ VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT };
  auto depst_src_scope = vk::Scope{ depst_stage, depst_src_access };
  auto input_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  auto input_src_access = VkAccessFlags{ 0 };
  auto input_dst_access = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
  auto input_src_scope = vk::Scope{ input_stage, input_src_access };
  for (auto [subpass_i, subpass] : subpasses | toy::enumerate) {
    for (auto color_i : subpass.colors) {
      auto add_dependency = [&](uint32_t src_subpass, vk::Scope src_scope) {
        dependencies[{ src_subpass, subpass_i }].push_back(VkSubpassDependency{
          .srcSubpass = src_subpass,
          .dstSubpass = subpass_i,
          .srcStageMask = src_scope.stage_mask,
          .dstStageMask = color_stage,
          .srcAccessMask = src_scope.access_mask,
          .dstAccessMask = color_dst_access,
        });
      };
      if (last_read.contains(color_i)) {
        for (auto read : last_read.at(color_i)) {
          add_dependency(read, input_src_scope);
        }
        last_read.erase(color_i);
      } else if (color_last_write.contains(color_i)) {
        add_dependency(color_last_write.at(color_i), color_src_scope);
      } else {
        add_dependency(VK_SUBPASS_EXTERNAL, attachments[color_i].enter_dep.scope);
      }
      color_last_write[color_i] = subpass_i;
    }
    if (subpass.depst_attachment.has_value()) {
      auto depst_i = subpass.depst_attachment.value();
      auto add_dependency = [&](uint32_t src_subpass, vk::Scope src_scope) {
        dependencies[{ src_subpass, subpass_i }].push_back(VkSubpassDependency{
          .srcSubpass = src_subpass,
          .dstSubpass = subpass_i,
          .srcStageMask = src_scope.stage_mask,
          .dstStageMask = depst_stage,
          .srcAccessMask = src_scope.access_mask,
          .dstAccessMask = depst_dst_access,
        });
      };
      if (last_read.contains(depst_i)) {
        for (auto read : last_read.at(depst_i)) {
          add_dependency(read, input_src_scope);
        }
        last_read.erase(depst_i);
      } else if (depst_last_write.contains(depst_i)) {
        add_dependency(depst_last_write.at(depst_i), depst_src_scope);
      } else {
        add_dependency(VK_SUBPASS_EXTERNAL, attachments[depst_i].enter_dep.scope);
      }
      depst_last_write[depst_i] = subpass_i;
    }
    for (auto& input_i : subpass.inputs) {
      auto add_dependency = [&](uint32_t src_subpass, vk::Scope src_scope) {
        dependencies[{ src_subpass, subpass_i }].push_back(VkSubpassDependency{
          .srcSubpass = src_subpass,
          .dstSubpass = subpass_i,
          .srcStageMask = src_scope.stage_mask,
          .dstStageMask = input_stage,
          .srcAccessMask = src_scope.access_mask,
          .dstAccessMask = input_dst_access,
        });
      };
      if (color_last_write.contains(input_i)) {
        add_dependency(color_last_write.at(input_i), color_src_scope);
      } else if (depst_last_write.contains(input_i)) {
        add_dependency(depst_last_write.at(input_i), depst_src_scope);
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
    auto& attachment = attachments[attachment_i];
    dependencies[{ subpass_i, VK_SUBPASS_EXTERNAL }].push_back(VkSubpassDependency{
      .srcSubpass = subpass_i,
      .dstSubpass = VK_SUBPASS_EXTERNAL,
      .srcStageMask = color_stage,
      .dstStageMask = attachment.exit_dep.scope.stage_mask,
      .srcAccessMask = color_src_access,
      .dstAccessMask = attachment.exit_dep.scope.access_mask,
    });
  }
  for (auto [attachment_i, subpass_i] : depst_last_write) {
    if (last_read.contains(attachment_i)) {
      continue;
    }
    auto& attachment = attachments[attachment_i];
    dependencies[{ subpass_i, VK_SUBPASS_EXTERNAL }].push_back(VkSubpassDependency{
      .srcSubpass = subpass_i,
      .dstSubpass = VK_SUBPASS_EXTERNAL,
      .srcStageMask = depst_stage,
      .dstStageMask = attachment.exit_dep.scope.stage_mask,
      .srcAccessMask = depst_src_access,
      .dstAccessMask = attachment.exit_dep.scope.access_mask,
    });
  }
  for (auto [attachment_i, subpasses] : last_read) {
    for (auto subpass_i : subpasses) {
      auto& attachment = attachments[attachment_i];
      dependencies[{ subpass_i, VK_SUBPASS_EXTERNAL }].push_back(VkSubpassDependency{
        .srcSubpass = subpass_i,
        .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = input_stage,
        .dstStageMask = attachment.exit_dep.scope.stage_mask,
        .srcAccessMask = input_src_access,
        .dstAccessMask = attachment.exit_dep.scope.access_mask,
      });
    }
  }
  auto merged_dependencies = //
    dependencies | views::values | views::transform([](auto& deps) {
      return VkSubpassDependency{
        .srcSubpass = deps[0].srcSubpass,
        .dstSubpass = deps[0].dstSubpass,
        .srcStageMask = std::reduce(
          deps.begin(),
          deps.end(),
          VkPipelineStageFlags{},
          [](auto a, auto& b) { return a | b.srcStageMask; }
        ),
        .dstStageMask = std::reduce(
          deps.begin(),
          deps.end(),
          VkPipelineStageFlags{},
          [](auto a, auto& b) { return a | b.dstStageMask; }
        ),
        .srcAccessMask = std::reduce(
          deps.begin(),
          deps.end(),
          VkAccessFlags{},
          [](auto a, auto& b) { return a | b.srcAccessMask; }
        ),
        .dstAccessMask = std::reduce(
          deps.begin(),
          deps.end(),
          VkAccessFlags{},
          [](auto a, auto& b) { return a | b.dstAccessMask; }
        ),
      };
    }) |
    ranges::to<std::vector>();
  auto render_pass_create_info = VkRenderPassCreateInfo{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = static_cast<uint32_t>(attachment_descs.size()),
    .pAttachments = attachment_descs.data(),
    .subpassCount = static_cast<uint32_t>(subpass_descriptions.size()),
    .pSubpasses = subpass_descriptions.data(),
    .dependencyCount = static_cast<uint32_t>(merged_dependencies.size()),
    .pDependencies = merged_dependencies.data(),
  };
  return { vk::Device::getInstance(), render_pass_create_info };
}

} // namespace rd::vk