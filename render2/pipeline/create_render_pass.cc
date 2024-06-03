module render.render_pass;

import "vulkan_config.h";
import render.vk.resource;
import render.vk.device;
import render.vk.sync;
import render.vertex;

import std;
import toy;

namespace rd {

auto RenderPass::createRenderPass(std::span<const SubpassInfo> subpasses)
  -> std::vector<AttachmentBuffers> {
  enum ExternalUsage {
    PRESENTABLE,
    MULTI_SAMPLE,
    HOST_READABLE,
  };
  auto attachments = std::vector<VkAttachmentDescription>{};
  auto attachment_buffers_flights = std::vector<AttachmentBuffers>{};
  attachment_buffers_flights.resize(FlightContext::flight_n);
  auto color_attachment_index = std::map<ColorFramebuf*, uint32_t>{};
  auto depst_attachment_index = std::map<DepthStencilFramebuf*, uint32_t>{};
  for (const auto& subpass : subpasses) {
    // for attachment description:
    // layout: for color, determine by isPresentable
    // loadOp: always clear
    // storeOp: always store
    // todo: if not presentable and user need check the content then store, else dont care
    auto get_attachment =
      [](VkFormat format, VkSampleCountFlagBits sample_count, ExternalUsage attachment_type) {
        auto layout_getter = [&]() {
          switch (attachment_type) {
          case PRESENTABLE:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
          case MULTI_SAMPLE:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
          case HOST_READABLE:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
          }
        };
        return VkAttachmentDescription{
          /**
           * @brief if color format, the stencilxxxOp is ignored;
           * if depth and/or stencil format, xxxOp apply to depth, stencilxxxOp apply to stencil
           */
          .format = format,
          .samples = sample_count,
          /**
           * @brief load op: define load operation behavior of color and depth
           * the load op happen in VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT(color
           * attachment) or VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT(depth attachment) and
           * happen before any command which access the sample in the render pass
           */
          // VK_ATTACHMENT_LOAD_OP_LOAD: 保留 attachment 中现有内容
          // VK_ATTACHMENT_LOAD_OP_CLEAR: 将其中内容清理为一个常量
          // VK_ATTACHMENT_LOAD_OP_DONT_CARE: 不在乎
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          /**
           * @brief store op: define store operation behavior of color and depth
           * the store op happen in VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT(color
           * attachment) or VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT(depth attachment) and
           * happen after any command which access the sample in the render pass
           */
          // VK_ATTACHMENT_STORE_OP_STORE: 渲染后内容存入内存稍后使用
          // VK_ATTACHMENT_STORE_DONT_CARE: 不在乎
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,

          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
          // 开启及结束时 要求 的图像布局
          // UNDEINFED init layout use with CLEAR load op together
          .initialLayout = layout_getter(),
          .finalLayout = layout_getter(),
        };
      };
    // check all inputs is already in color_attachment_index or depst_attachment_index, and
    // not in depth or colors of current subpass
    for (auto input : subpass.inputs) {
      toy::throwf(
        std::visit(
          [&](auto* framebuf) {
            using T = decltype(framebuf);
            if constexpr (std::same_as<T, ColorFramebuf*>) {
              return color_attachment_index.contains(framebuf);
            } else if constexpr (std::same_as<T, DepthStencilFramebuf*>) {
              return depst_attachment_index.contains(framebuf);
            } else {
              toy::throwf("unsupport framebuf type");
            }
          },
          input
        ),
        "the input attachment is not in color_attachment_index or depst_attachment_index"
      );
      toy::throwf(
        std::visit(
          [&](auto* framebuf) {
            using T = decltype(framebuf);
            if constexpr (std::same_as<T, ColorFramebuf*>) {
              return ranges::find(subpass.colors, framebuf) == subpass.colors.end();
            } else if constexpr (std::same_as<T, DepthStencilFramebuf*>) {
              return !subpass.depth_stencil.has_value() ||
                     subpass.depth_stencil.value() == framebuf;
            } else {
              toy::throwf("unsupport framebuf type");
            }
          },
          input
        ),
        "the input attachment is in depth or colors of current subpass"
      );
    }

    for (auto* color : subpass.colors) {
      if (color_attachment_index.contains(color)) {
        continue;
      }
      color_attachment_index[color] = attachments.size();
      attachments.push_back(get_attachment(
        color->getFormat(),
        VK_SAMPLE_COUNT_1_BIT,
        color->isPresentable() ? PRESENTABLE : HOST_READABLE
      ));
      for (auto i = 0; i < FlightContext::flight_n; i++) {
        attachment_buffers_flights[i].push_back(color->getImageView(i));
      }
      if (color->getSampleCount() != VK_SAMPLE_COUNT_1_BIT) {
        attachments.push_back(
          get_attachment(color->getFormat(), color->getSampleCount(), MULTI_SAMPLE)
        );
        for (auto i = 0; i < FlightContext::flight_n; i++) {
          attachment_buffers_flights[i].push_back(color->getMultiSampleImageView(i));
        }
      }
    }
    if (subpass.depth_stencil.has_value()) {
      auto* depst = subpass.depth_stencil.value();
      if (!depst_attachment_index.contains(depst)) {
        depst_attachment_index[depst] = attachments.size();
        attachments.push_back(
          get_attachment(depst->getFormat(), VK_SAMPLE_COUNT_1_BIT, HOST_READABLE)
        );
        for (auto i = 0; i < FlightContext::flight_n; i++) {
          attachment_buffers_flights[i].push_back(depst->getImageView(i));
        }
      }
    }
  }

  /**
   * @brief get subpass descriptions
   *
   */
  auto subpass_descriptions = std::vector<VkSubpassDescription>{};
  auto attachment_refs = std::vector<VkAttachmentReference>{};
  for (const auto& subpass : subpasses) {
    auto color_index = static_cast<uint32_t>(attachment_refs.size());
    for (auto* color : subpass.colors) {
      if (color->getSampleCount() == VK_SAMPLE_COUNT_1_BIT) {
        attachment_refs.push_back(VkAttachmentReference{
          // 引用的 attachment 的索引
          .attachment = color_attachment_index.at(color),
          // 用到该 ref 的 subpass 过程中使用的布局，会自动转换
          // if enable multi sample, resolve op also occur in color attachment ouput stage
          .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        });
      } else {
        attachment_refs.push_back(VkAttachmentReference{
          .attachment = color_attachment_index.at(color) + 1,
          .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        });
      }
    }
    auto resolve_index = static_cast<uint32_t>(attachment_refs.size());
    for (auto* color : subpass.colors) {
      if (color->getSampleCount() == VK_SAMPLE_COUNT_1_BIT) {
        attachment_refs.push_back(VkAttachmentReference{
          .attachment = VK_ATTACHMENT_UNUSED,
        });
      } else {
        attachment_refs.push_back(VkAttachmentReference{
          .attachment = color_attachment_index.at(color),
          .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        });
      }
    }
    auto input_index = static_cast<uint32_t>(attachment_refs.size());
    for (auto& input : subpass.inputs) {
      std::visit(
        [&](auto* framebuf) {
          using T = decltype(framebuf);
          if constexpr (std::same_as<T, ColorFramebuf*>) {
            attachment_refs.push_back(VkAttachmentReference{
              .attachment = color_attachment_index.at(framebuf),
              .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            });
          } else if constexpr (std::same_as<T, DepthStencilFramebuf*>) {
            attachment_refs.push_back(VkAttachmentReference{
              .attachment = depst_attachment_index.at(framebuf),
              .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            });
          } else {
            toy::throwf("unsupport framebuf type");
          }
        },
        input
      );
    }
    auto depth_ref = (VkAttachmentReference*){};
    if (subpass.depth_stencil.has_value()) {
      attachment_refs.push_back(VkAttachmentReference{
        .attachment = depst_attachment_index.at(subpass.depth_stencil.value()),
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      });
      depth_ref = &attachment_refs.back();
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
      .pDepthStencilAttachment = depth_ref,
      // pPreserveAttachments: Attachments that are not used by this subpass, but
      // for which the data must be preserved
    });
  }

  /**
   * @brief get subpass dependencies
   *
   */
  auto dependencies = std::map<std::pair<uint32_t, uint32_t>, std::vector<VkSubpassDependency>>{};
  // Only one is needed because only one write can occur simultaneously, on the contrary,
  // multiple reads is needed

  // Either there is one write, or there are multiple reads
  // For certine frambuf, the reads and write can contains subpass at the same time, the write is
  // before the reads
  auto color_last_write = std::map<ColorFramebuf*, uint32_t>{};
  auto color_last_reads = std::map<ColorFramebuf*, std::vector<uint32_t>>{};
  auto depst_last_write = std::map<DepthStencilFramebuf*, uint32_t>{};
  auto depst_last_reads = std::map<DepthStencilFramebuf*, std::vector<uint32_t>>{};

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

  auto get_external_scope = [&](ExternalUsage usage) {
    switch (usage) {
    case HOST_READABLE:
      return vk::Scope{
        .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .access_mask = 0,
      };
    case PRESENTABLE:
      return vk::Scope{
        .stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .access_mask = 0,
      };
    default:
      toy::throwf("only host readable and presentable attachment need external dependency");
    }
  };

  for (auto [subpass_i, subpass] : subpasses | toy::enumerate) {
    for (auto* color : subpass.colors) {
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
      if (color_last_reads.contains(color)) {
        for (auto read : color_last_reads.at(color)) {
          add_dependency(read, input_src_scope);
        }
        color_last_reads.erase(color);
      } else if (color_last_write.contains(color)) {
        add_dependency(color_last_write.at(color), color_src_scope);
      } else {
        if (color->isPresentable()) {
          add_dependency(VK_SUBPASS_EXTERNAL, get_external_scope(PRESENTABLE));
        } else {
          add_dependency(VK_SUBPASS_EXTERNAL, get_external_scope(HOST_READABLE));
        }
      }
      color_last_write[color] = subpass_i;
    }
    if (subpass.depth_stencil.has_value()) {
      auto* depst = subpass.depth_stencil.value();
      auto  add_dependency = [&](uint32_t src_subpass, vk::Scope src_scope) {
        dependencies[{ src_subpass, subpass_i }].push_back(VkSubpassDependency{
           .srcSubpass = src_subpass,
           .dstSubpass = subpass_i,
           .srcStageMask = src_scope.stage_mask,
           .dstStageMask = depst_stage,
           .srcAccessMask = src_scope.access_mask,
           .dstAccessMask = depst_dst_access,
        });
      };
      if (depst_last_reads.contains(depst)) {
        for (auto read : depst_last_reads.at(depst)) {
          add_dependency(read, input_src_scope);
        }
        depst_last_reads.erase(depst);
      } else if (depst_last_write.contains(depst)) {
        add_dependency(depst_last_write.at(depst), depst_src_scope);
      } else {
        add_dependency(VK_SUBPASS_EXTERNAL, get_external_scope(HOST_READABLE));
      }
      depst_last_write[depst] = subpass_i;
    }
    for (auto& input : subpass.inputs) {
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
      std::visit(
        [&](auto* framebuf) {
          using T = decltype(framebuf);
          if constexpr (std::same_as<T, ColorFramebuf*>) {
            if (color_last_write.contains(framebuf)) {
              add_dependency(color_last_write.at(framebuf), color_src_scope);
            } else {
              toy::throwf("the input attachment appeared before a write");
            }
            color_last_reads[framebuf].push_back(subpass_i);
          } else if constexpr (std::same_as<T, DepthStencilFramebuf*>) {
            if (depst_last_write.contains(framebuf)) {
              add_dependency(depst_last_write.at(framebuf), depst_src_scope);
            } else {
              toy::throwf("the input attachment appeared before a write");
            }
            depst_last_reads[framebuf].push_back(subpass_i);
          } else {
            toy::throwf("unsupport framebuf type");
          }
        },
        input
      );
    }
  }
  auto merged_dependencies = dependencies | views::values | views::transform([](auto& deps) {
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
    .attachmentCount = static_cast<uint32_t>(attachments.size()),
    .pAttachments = attachments.data(),
    .subpassCount = static_cast<uint32_t>(subpass_descriptions.size()),
    .pSubpasses = subpass_descriptions.data(),
    .dependencyCount = static_cast<uint32_t>(merged_dependencies.size()),
    .pDependencies = merged_dependencies.data(),
  };
  _render_pass = { vk::Device::getInstance(), render_pass_create_info };
}

} // namespace rd