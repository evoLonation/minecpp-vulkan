module render.vk.reflections;
import std;
import toy;

import "vulkan_config.h";

namespace rd::vk::refl {

#define CASE(x)                                                                                    \
  case x:                                                                                          \
    return #x
auto imageLayout(VkImageLayout image_layout) -> std::string_view {
  switch (image_layout) {
    CASE(VK_IMAGE_LAYOUT_UNDEFINED);
    CASE(VK_IMAGE_LAYOUT_PREINITIALIZED);
    CASE(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CASE(VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    CASE(VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    CASE(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CASE(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    CASE(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CASE(VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL);
    CASE(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL);
    CASE(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  default:
    toy::throwf("unknown VkImageLayout: {}", static_cast<size_t>(image_layout));
  }
}

auto stageFlag(VkPipelineStageFlagBits2 stage) -> std::string_view {
  switch (stage) {
    CASE(VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
    CASE(VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
    CASE(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    CASE(VK_PIPELINE_STAGE_TRANSFER_BIT);
    CASE(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    CASE(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    CASE(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    CASE(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    CASE(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    CASE(VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT);
    CASE(VK_PIPELINE_STAGE_NONE);
  default:
    toy::throwf("unknown VkPipelineStageFlagBits2: {}", static_cast<size_t>(stage));
  }
}

auto accessFlag(VkAccessFlagBits2 access) -> std::string_view {
  switch (access) {
    CASE(VK_ACCESS_INDEX_READ_BIT);
    CASE(VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
    CASE(VK_ACCESS_UNIFORM_READ_BIT);
    CASE(VK_ACCESS_INPUT_ATTACHMENT_READ_BIT);
    CASE(VK_ACCESS_SHADER_READ_BIT);
    CASE(VK_ACCESS_SHADER_WRITE_BIT);
    CASE(VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
    CASE(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    CASE(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    CASE(VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    CASE(VK_ACCESS_TRANSFER_READ_BIT);
    CASE(VK_ACCESS_TRANSFER_WRITE_BIT);
    CASE(VK_ACCESS_NONE);
  default:
    toy::throwf("unknown VkAccessFlagBits2: {}", static_cast<size_t>(access));
  };
}

auto format(VkFormat format) -> std::string_view {
  switch (format) {
    CASE(VK_FORMAT_R8G8B8A8_SRGB);

    CASE(VK_FORMAT_D16_UNORM);
    CASE(VK_FORMAT_D32_SFLOAT);
    CASE(VK_FORMAT_D24_UNORM_S8_UINT);
    CASE(VK_FORMAT_S8_UINT);

    CASE(VK_FORMAT_R32G32_SFLOAT);
    CASE(VK_FORMAT_R32G32B32_SFLOAT);
    CASE(VK_FORMAT_R32G32B32A32_SFLOAT);
    CASE(VK_FORMAT_R32_SFLOAT);
    CASE(VK_FORMAT_R64G64_SFLOAT);
    CASE(VK_FORMAT_R64G64B64_SFLOAT);
    CASE(VK_FORMAT_R64G64B64A64_SFLOAT);
    CASE(VK_FORMAT_R64_SFLOAT);
  default:
    toy::throwf("unknown VkFormat: {}", static_cast<size_t>(format));
  }
}

#undef CASE
} // namespace rd::vk::refl