module render.vk.command;

import render.vk.device;
import render.vk.tool;

namespace rd::vk {

auto createCommandPool(uint32_t family_index, bool short_live) -> rs::CommandPool {
  auto pool_create_info = VkCommandPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：允许重置单个command
    // buffer，否则就要重置命令池里的所有buffer
    // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT: 命令缓冲区会很频繁的记录新命令
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = family_index,
  };
  if (short_live) {
    pool_create_info.flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  }
  return { Device::getInstance(), pool_create_info };
}

auto allocateCommandBuffers(VkCommandPool command_pool, uint32_t count) -> rs::CommandBuffers {
  auto cbuffer_alloc_info = VkCommandBufferAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = command_pool,
    // VK_COMMAND_BUFFER_LEVEL_PRIMARY: 主缓冲区，类似于main
    // VK_COMMAND_BUFFER_LEVEL_SECONDARY: 次缓冲区，可复用，类似于其他函数
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = count,
  };
  return { Device::getInstance(), cbuffer_alloc_info };
}

void beginRecord(VkCommandBuffer cmdbuf) {
  // vkBeginCommandBuffer 会隐式执行vkResetCommandBuffer
  // vkResetCommandBuffer(worker.command_buffer, 0);
  auto begin_info = VkCommandBufferBeginInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    /** \param VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT specifies that each
     * recording of the command buffer will only be submitted once, and the
     * command buffer will be reset and recorded again between each submission.
     * \param VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT specifies that a
     * secondary command buffer is considered to be entirely inside a render
     * pass. If this is a primary command buffer, then this bit is ignored.
     * \param VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT specifies that a
     * command buffer can be resubmitted to any queue of the same queue family
     * while it is in the pending state, and recorded into multiple primary
     * command buffers.*/
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    .pInheritanceInfo = nullptr,
  };
  checkVkResult(vkBeginCommandBuffer(cmdbuf, &begin_info), "begin command buffer");
}
void endAndSubmitRecord(
  VkCommandBuffer                cmdbuf,
  VkQueue                        queue,
  std::span<const WaitSemaphore> wait_infos,
  std::span<const VkSemaphore>   signal_semas,
  VkFence                        signal_fence
) {
  checkVkResult(vkEndCommandBuffer(cmdbuf), "end command buffer");

  auto wait_semas = wait_infos | views::transform([](const auto& pair) { return pair.sema; }) |
                    ranges::to<std::vector>();
  auto wait_stages = wait_infos |
                     views::transform([](const auto& pair) { return pair.stage_mask; }) |
                     ranges::to<std::vector>();
  auto submit_info = VkSubmitInfo{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext = nullptr,
    .waitSemaphoreCount = (uint32_t)wait_infos.size(),
    // 对 pWaitSemaphores 中的每个 semaphore 都定义了 semaphore wait operation
    // 触发阶段由 dst stage mask 定义
    .pWaitSemaphores = wait_semas.data(),
    .pWaitDstStageMask = wait_stages.data(),
    .commandBufferCount = 1,
    .pCommandBuffers = &cmdbuf,
    .signalSemaphoreCount = (uint32_t)signal_semas.size(),
    .pSignalSemaphores = signal_semas.data(),
  };
  checkVkResult(vkQueueSubmit(queue, 1, &submit_info, signal_fence), "submit queue");
}

} // namespace rd::vk