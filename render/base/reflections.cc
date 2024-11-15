module render.reflections;
import std;
import toy;

import <vulkan_config.h>;

namespace rd::refl {

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

auto colorSpace(VkColorSpaceKHR color_space) -> std::string_view {
  switch (color_space) {
    CASE(VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
    CASE(VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT);
    CASE(VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
    CASE(VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT);
    CASE(VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT);
    CASE(VK_COLOR_SPACE_BT709_LINEAR_EXT);
    CASE(VK_COLOR_SPACE_BT709_NONLINEAR_EXT);
    CASE(VK_COLOR_SPACE_BT2020_LINEAR_EXT);
    CASE(VK_COLOR_SPACE_HDR10_ST2084_EXT);
    CASE(VK_COLOR_SPACE_DOLBYVISION_EXT);
    CASE(VK_COLOR_SPACE_HDR10_HLG_EXT);
    CASE(VK_COLOR_SPACE_ADOBERGB_LINEAR_EXT);
    CASE(VK_COLOR_SPACE_ADOBERGB_NONLINEAR_EXT);
    CASE(VK_COLOR_SPACE_PASS_THROUGH_EXT);
    CASE(VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT);
    CASE(VK_COLOR_SPACE_DISPLAY_NATIVE_AMD);
    CASE(VK_COLOR_SPACE_MAX_ENUM_KHR);
  };
}

auto result(VkResult result) -> std::string_view {
  switch (result) {
    CASE(VK_SUCCESS);
    CASE(VK_NOT_READY);
    CASE(VK_TIMEOUT);
    CASE(VK_EVENT_SET);
    CASE(VK_EVENT_RESET);
    CASE(VK_INCOMPLETE);
    CASE(VK_SUBOPTIMAL_KHR);
    CASE(VK_ERROR_OUT_OF_DATE_KHR);
  default:
    toy::throwf("unknown VkResult: {}", static_cast<size_t>(result));
  }
}

auto sampleCount(VkSampleCountFlagBits sample_count) -> std::string_view {
  switch (sample_count) {
    CASE(VK_SAMPLE_COUNT_1_BIT);
    CASE(VK_SAMPLE_COUNT_2_BIT);
    CASE(VK_SAMPLE_COUNT_4_BIT);
    CASE(VK_SAMPLE_COUNT_8_BIT);
    CASE(VK_SAMPLE_COUNT_16_BIT);
    CASE(VK_SAMPLE_COUNT_32_BIT);
    CASE(VK_SAMPLE_COUNT_64_BIT);
  default:
    toy::throwf("unknown VkSampleCountFlagBits: {}", static_cast<size_t>(sample_count));
  }
}

#undef CASE
} // namespace rd::refl