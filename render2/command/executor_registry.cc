module render.vk.executor;

import "vulkan_config.h";

import std;
import toy;

namespace rd::vk {

auto checkGraphicQueue(const QueueFamilyCheckContext& ctx) -> bool {
  return ctx.properties.queueFlags & VK_QUEUE_GRAPHICS_BIT;
}
auto checkPresentQueue(const QueueFamilyCheckContext& ctx) -> bool {
  VkBool32 presentSupport = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(ctx.device, ctx.index, ctx.surface, &presentSupport);
  return presentSupport == VK_TRUE;
}
auto checkTransferQueue(const QueueFamilyCheckContext& ctx) -> bool {
  // 支持 graphics 和 compute operation 的 queue 也必定支持 transfer operation
  return ctx.properties.queueFlags &
         (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT);
}

auto CommandExecutor::registerFamilies()
  -> std::vector<std::pair<QueueFamily, QueueFamilyRequestor>> {
  return {
    { QueueFamily::GRAPHICS, QueueFamilyRequestor{ 3, vk::checkGraphicQueue } },
    { QueueFamily::PRESENT, QueueFamilyRequestor{ 2, vk::checkPresentQueue } },
    { QueueFamily::TRANSFER, QueueFamilyRequestor{ 1, vk::checkTransferQueue } },
  };
}

} // namespace rd::vk