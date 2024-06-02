module render.vk.format;

import std;
import toy;

import "vulkan_config.h";

namespace rd::vk {

auto checkFormatSupport(
  VkPhysicalDevice          pdevice,
  FormatTarget              type,
  VkFormatFeatureFlags      features,
  std::span<const VkFormat> format_set
) -> bool {
  return ranges::all_of(format_set, [&](auto format) {
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(pdevice, format, &properties);
    auto support_features = (VkFormatFeatureFlags)0;
    switch (type) {
    case FormatTarget::BUFFER:
      support_features = properties.bufferFeatures;
      break;
    case FormatTarget::OPTIMAL_TILING:
      support_features = properties.optimalTilingFeatures;
      break;
    case FormatTarget::LINEAR_TILING:
      support_features = properties.linearTilingFeatures;
      break;
    }
    return (support_features & features) == features;
  });
  return true;
}

} // namespace rd::vk