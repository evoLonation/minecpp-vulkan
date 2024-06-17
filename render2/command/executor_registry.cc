module render.vk.executor;

import "vulkan_config.h";

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

void CommandExecutor::buildQueueExecutors() {
  auto queue_base_indices = std::map<QueueFamily, uint32_t>{};

  auto buildQueueExecutor = [&](QueueFamily family, uint32_t queue_number) -> QueueExecutor {
    auto& old_base_index = queue_base_indices[family];
    auto  executor = QueueExecutor{
      family,
       { old_base_index, old_base_index + queue_number },
    };
    old_base_index += queue_number;
    toy::throwf(
      old_base_index <= _cmd_contexts[family].queues.size(),
      "the queue executor's queue number is out of range"
    );
    return executor;
  };
  using namespace executors;
  copy = buildQueueExecutor(QueueFamily::TRANSFER, 1);
  present = buildQueueExecutor(QueueFamily::PRESENT, 2);
  tool = buildQueueExecutor(QueueFamily::GRAPHICS, 1);
  render = buildQueueExecutor(QueueFamily::GRAPHICS, 2);
}

} // namespace rd::vk