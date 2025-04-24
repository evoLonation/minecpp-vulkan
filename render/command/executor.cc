module;
#include <toy.h>
#include <vulkan_tool.h>
module render.executor;

import render.tool;

import <vulkan_config.h>;

namespace rd {

void CommandBuffer::record(std::function<void(VkCommandBuffer cmdbuf)> const& recorder) {
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
  CHECK_VK_RESULT(vkBeginCommandBuffer(get(), &begin_info));
  recorder(get());
  CHECK_VK_RESULT(vkEndCommandBuffer(get()));
}

CommandBufferPool::CommandBufferPool(uint32 family_index) {
  _pool = rs::CommandPool{ VkCommandPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：允许重置单个command
    // buffer，否则就要重置命令池里的所有buffer
    // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT: 命令缓冲区会很频繁的记录新命令
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
    .queueFamilyIndex = family_index,
  } };
}

void CommandBufferPool::workingToIdle() {
  for (auto iter = _working_cmdbufs.begin(); iter != _working_cmdbufs.end();) {
    if (iter->getSema().wait(0)) {
      _idle_cmdbufs.push_back(std::move(*iter));
      iter = _working_cmdbufs.erase(iter);
    } else {
      iter++;
    }
  }
}

void CommandBufferPool::expand() {
  auto cmdbufs = rs::CommandBuffers{ VkCommandBufferAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = _pool,
    // VK_COMMAND_BUFFER_LEVEL_PRIMARY: 主缓冲区，类似于main
    // VK_COMMAND_BUFFER_LEVEL_SECONDARY: 次缓冲区，可复用，类似于其他函数
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 5,
  } };
  _idle_cmdbufs.append_range(
    rs::CommandBuffers::split(std::move(cmdbufs)) |
    views::transform([](rs::CommandBuffer& cmdbuf) { return CommandBuffer(std::move(cmdbuf)); })
  );
}

void CommandBufferPool::tryShrink() {
  // todo: better shrink strategy
  auto shrink_size = 1000;
  if (_idle_cmdbufs.size() <= shrink_size) {
    return;
  }
  toy::debugf("shrink {} -> {}", _idle_cmdbufs.size(), shrink_size);
  auto range = _idle_cmdbufs | views::transform([](CommandBuffer& cmdbuf) -> decltype(auto) {
                 return static_cast<rs::CommandBuffer&>(cmdbuf);
               }) |
               views::drop(shrink_size);
  rs::CommandBuffers::join(range);
  _idle_cmdbufs.erase(_idle_cmdbufs.begin() + shrink_size, _idle_cmdbufs.end());
}

auto CommandBufferPool::extract() -> CommandBuffer {
  auto ret = std::move(_idle_cmdbufs.back());
  ret.getSema().pendingNewSignal();
  _idle_cmdbufs.pop_back();
  return ret;
}

auto Waitable::wait(VkPipelineStageFlags2 stage, uint64 nano_timeout) -> bool {
  if (stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) {
    return _cmdbuf.getSema().wait(nano_timeout);
  }
  return _stage_semas.at(stage).wait(nano_timeout);
}

auto Waitable::wait(
  std::span<std::pair<Waitable*, VkPipelineStageFlags2> const> waits, bool any, uint64 nano_timeout
) -> bool {
  auto semas = std::vector<IncrementalSemaphore*>{};
  for (auto& [waitable, stage] : waits) {
    if (stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) {
      semas.push_back(&waitable->_cmdbuf.getSema());
    } else {
      semas.push_back(&waitable->_stage_semas.at(stage));
    }
  }
  return IncrementalSemaphore::wait(semas, any, nano_timeout);
}

auto Waitable::getWaitInfo(VkPipelineStageFlags2 stage) -> std::pair<VkSemaphore, uint64> {
  if (stage == VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) {
    return _cmdbuf.getSema().getDeviceSyncInfo();
  }
  return _stage_semas.at(stage).getDeviceSyncInfo();
}

/**
 * @brief submit a command batch to device, return the waitable. Waitable can be waited for
 * stages in batch.signals and ALL_COMMANDS_BIT on host (call waitable.wait()) and device
 * (call submit()).
 *
 * @param batch the batch to submit
 * @return Waitable
 */
auto CommandExecutor::submit(CommandBatch const& batch) -> Waitable {
  auto [submit_info, waitable] = getSubmitInfo(batch);
  CHECK_VK_RESULT(vkQueueSubmit2KHR(_queue, 1, &submit_info.info, VK_NULL_HANDLE));
  return std::move(waitable);
}

auto CommandExecutor::submit(std::span<CommandBatch const> batches) -> std::vector<Waitable> {
  auto submit_infos = std::vector<SubmitInfo>{};
  auto vk_submit_infos = std::vector<VkSubmitInfo2>{};
  auto waitables = std::vector<Waitable>{};
  for (auto& batch : batches) {
    auto [submit_info, waitable] = getSubmitInfo(batch);
    submit_infos.push_back(std::move(submit_info));
    vk_submit_infos.push_back(submit_info.info);
    waitables.push_back(std::move(waitable));
  }
  CHECK_VK_RESULT(
    vkQueueSubmit2(_queue, vk_submit_infos.size(), vk_submit_infos.data(), VK_NULL_HANDLE)
  );
  return waitables;
}

auto CommandExecutor::getWaitInfos(std::vector<CommandBatch::WaitInfo> const& waits)
  -> std::vector<VkSemaphoreSubmitInfo> {
  auto wait_infos = std::vector<VkSemaphoreSubmitInfo>{};
  for (auto& wait : waits) {
    auto [sema, value] = wait.first->getWaitInfo(wait.second);
    wait_infos.push_back(
      VkSemaphoreSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = sema,
        .value = value,
        .stageMask = wait.second,
      }
    );
  }
  return wait_infos;
}

auto CommandExecutor::getWaitInfos(std::vector<CommandBatch::RawWaitInfo> const& raw_waits)
  -> std::vector<VkSemaphoreSubmitInfo> {
  auto wait_infos = std::vector<VkSemaphoreSubmitInfo>{};
  for (auto& [sema, stage] : raw_waits) {
    wait_infos.push_back(
      VkSemaphoreSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = sema,
        .stageMask = stage,
      }
    );
  }
  return wait_infos;
}

auto CommandExecutor::getCmdbufSignalInfo(CommandBuffer& cmdbuf) -> VkSemaphoreSubmitInfo {
  auto [sema, value] = cmdbuf.getSema().getDeviceSyncInfo();
  return VkSemaphoreSubmitInfo{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    .semaphore = sema,
    .value = value,
    .stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
  };
}

auto CommandExecutor::getSignalInfos(std::vector<CommandBatch::RawSignalInfo> const& signals)
  -> std::pair<std::vector<VkSemaphoreSubmitInfo>, Waitable::StageSemaphoreMap> {
  return { getWaitInfos(signals), {} };
}

auto CommandExecutor::getSignalInfos(std::vector<CommandBatch::SignalInfo> const& signals)
  -> std::pair<std::vector<VkSemaphoreSubmitInfo>, Waitable::StageSemaphoreMap> {
  auto signal_infos = std::vector<VkSemaphoreSubmitInfo>{};
  auto stage_sema_map = Waitable::StageSemaphoreMap{};
  for (auto& signal : signals) {
    auto sema = DisposableSemaphore{ *_sema_pool };
    auto [handle, value] = sema.getDeviceSyncInfo();
    signal_infos.push_back(
      VkSemaphoreSubmitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = handle,
        .value = value,
        .stageMask = signal,
      }
    );
    stage_sema_map.emplace(signal, std::move(sema));
  }
  return { std::move(signal_infos), std::move(stage_sema_map) };
}

auto CommandExecutor::getSubmitInfo(CommandBatch const& batch) -> std::pair<SubmitInfo, Waitable> {
  auto cmdbuf = DisposableCmdbuf{ _cmdbuf_pool };
  cmdbuf.record(batch.recorder);
  auto cmdbuf_info = std::make_unique<VkCommandBufferSubmitInfo>(VkCommandBufferSubmitInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
    .commandBuffer = cmdbuf.get(),
  });
  auto wait_infos = std::vector<VkSemaphoreSubmitInfo>{};
  wait_infos.append_range(getWaitInfos(batch.waits));
  wait_infos.append_range(getWaitInfos(batch.raw_waits));
  auto signal_infos = std::vector<VkSemaphoreSubmitInfo>{};
  auto stage_sema_map = Waitable::StageSemaphoreMap{};
  signal_infos.push_back(getCmdbufSignalInfo(cmdbuf));
  auto signal_ret = getSignalInfos(batch.signals);
  signal_infos.append_range(signal_ret.first);
  stage_sema_map.merge(std::move(signal_ret.second));
  auto raw_signal_ret = getSignalInfos(batch.raw_signals);
  signal_infos.append_range(raw_signal_ret.first);
  stage_sema_map.merge(std::move(raw_signal_ret.second));
  auto waitable = Waitable{ std::move(cmdbuf), std::move(stage_sema_map) };
  auto submit_info = VkSubmitInfo2{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .waitSemaphoreInfoCount = static_cast<uint32>(wait_infos.size()),
    .pWaitSemaphoreInfos = wait_infos.data(),
    .commandBufferInfoCount = 1,
    .pCommandBufferInfos = cmdbuf_info.get(),
    .signalSemaphoreInfoCount = static_cast<uint32>(signal_infos.size()),
    .pSignalSemaphoreInfos = signal_infos.data(),
  };
  return std::pair{
    SubmitInfo{
      .info = submit_info,
      .wait_infos = std::move(wait_infos),
      .signal_infos = std::move(signal_infos),
      .cmdbuf_info = std::move(cmdbuf_info),
    },
    std::move(waitable),
  };
}

ExecutorManager::ExecutorManager(std::span<std::pair<EnumT, FamilyQueueCount> const> family_infos) {
  for (auto& [family, info] : family_infos) {
    auto& [family_i, count] = info;
    _families[family] = family_i;
    for (auto queue_index : views::iota(0u, count)) {
      _executors[family_i].emplace_back(family_i, queue_index, &_sema_pool);
    }
  }
}

} // namespace rd