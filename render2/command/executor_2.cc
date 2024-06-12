module render.vk.executor;

import "vulkan_config.h";
import render.vk.tool;
import render.vk.resource;
import render.vk.surface;
import render.vk.device;
import render.vk.sync;
import render.vk.command;

import std;
import toy;

namespace rd::vk {

auto Waitable::consume() -> SemaphoreRef {
  if (_semas.empty()) {
    toy::throwf("The waitable has no semaphore!");
  }
  auto sema = std::move(_semas.back());
  _semas.pop_back();
  return sema;
}

auto CommandExecutor::addWorkingFence() -> FenceRef {
  auto  working_ = WorkingFence{ _fence_pool };
  auto& working = _working_fences.emplace(working_.fence->get(), std::move(working_)).first->second;
  return { working.fence.get(), working.borrowed };
}

auto CommandExecutor::addWorkingSemas(uint32_t sema_n) -> std::vector<SemaphoreRef> {
  auto borrowed_semas = std::vector<SemaphoreRef>{};
  while (sema_n--) {
    auto  working_ = WorkingSemaphore{ _sema_pool };
    auto& working = _working_semas.emplace(working_.sema->get(), std::move(working_)).first->second;
    borrowed_semas.emplace_back(working.sema.get(), working.borrowed);
  }
  return borrowed_semas;
}

auto CommandExecutor::addWorkingCmdbuf(
  CmdbufPool&              cmdbuf_pool,
  WorkingCmdbufs&          working_cmdbufs,
  Fence*                   fence,
  std::vector<VkSemaphore> wait_semas,
  std::vector<VkSemaphore> signal_semas
) -> VkCommandBuffer {
  auto cmd_working =
    WorkingCmdbuf{ cmdbuf_pool, fence, std::move(wait_semas), std::move(signal_semas) };
  auto cmdbuf = cmd_working.cmdbuf.get();
  working_cmdbufs.emplace(cmd_working.cmdbuf.get(), std::move(cmd_working));
  return cmdbuf;
}

auto CommandExecutor::getQueueInfo() -> QueueInfo {
  auto info_queue_priorities =
    _queue_meta | views::values | views::transform([](auto& info) {
      return views::repeat(1.0f) | views::take(info.queue_number) | ranges::to<std::vector>();
    }) |
    ranges::to<std::vector>();
  auto queue_create_infos = views::zip(_queue_meta | views::values, info_queue_priorities) |
                            views::transform([](auto pair) {
                              auto& [family_info, priorities] = pair;
                              return VkDeviceQueueCreateInfo{
                                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                .queueFamilyIndex = family_info.family_index,
                                .queueCount = static_cast<uint32_t>(family_info.queue_number),
                                .pQueuePriorities = priorities.data(),
                              };
                            }) |
                            ranges::to<std::vector>();

  return QueueInfo{ std::move(info_queue_priorities), std::move(queue_create_infos) };
}

CommandExecutor::CommandExecutor() {
  auto queues = _queue_meta |
                views::transform([device = Device::getInstance().get()](auto& key_value) {
                  auto& [family, pair] = key_value;
                  auto& [family_index, queue_n] = pair;
                  return std::pair{
                    family,
                    views::iota(0u, queue_n) | views::transform([&](auto queue_index) {
                      VkQueue queue;
                      vkGetDeviceQueue(device, family_index, queue_index, &queue);
                      return queue;
                    }) |
                      ranges::to<std::vector>(),
                  };
                }) |
                ranges::to<std::map>();
  for (auto& [family, info] : _queue_meta) {
    _cmd_contexts.emplace(
      family, CommandContext{ info.family_index, queues[family], info.family_index }
    );
  }
  _thread = std::thread{ &CommandExecutor::task, this };
  _task_done = false;
  buildQueueExecutors();
}

CommandExecutor::~CommandExecutor() {
  _task_done = true;
  _thread.join();
}

void CommandExecutor::task() {
  while (
    !(_task_done && _working_fences.empty() && _working_semas.empty() &&
      ranges::all_of(
        _cmd_contexts | views::values, [](auto& ctx) { return ctx.working_cmdbufs.empty(); }
      ))
  ) {
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(20ms);
    collect();
  }
}

void CommandExecutor::collect() {
  auto guard = std::lock_guard<std::mutex>{ _mutex };
  // DO NOT erase element in range for iteration, if need erase, do like this
  for (auto& ctx : _cmd_contexts | views::values) {
    auto& working_cmdbufs = ctx.working_cmdbufs;
    auto& cmdbuf_pool = ctx.cmdbuf_pool;
    for (auto iter = working_cmdbufs.begin(); iter != working_cmdbufs.end();) {
      auto& info = iter->second;
      if (info.fence->isSignaled()) {
        _working_fences.at(info.fence->get()).cmd_done = true;
        for (auto sema : info.signal_semas) {
          _working_semas.at(sema).signal_cmd_done = true;
        }
        for (auto sema : info.wait_semas) {
          _working_semas.at(sema).wait_cmd_done = true;
        }
        working_cmdbufs.erase(iter++);
      } else {
        iter++;
      }
    }
  }
  for (auto iter = _working_fences.begin(); iter != _working_fences.end();) {
    auto& info = iter->second;
    if (!info.borrowed && info.cmd_done) {
      info.fence->reset();
      _working_fences.erase(iter++);
    } else {
      iter++;
    }
  }
  for (auto iter = _working_semas.begin(); iter != _working_semas.end();) {
    auto& info = iter->second;
    if (!info.borrowed && info.signal_cmd_done && info.wait_cmd_done) {
      _working_semas.erase(iter++);
    } else {
      iter++;
    }
  }
}

auto CommandExecutor::submitImpl(
  QueueFamily family, uint32_t queue_index, std::span<WaitInfo const> wait_infos, uint32_t signal_n
) -> SubmitImplResult {
  auto result = SubmitImplResult{};
  auto guard = std::lock_guard<std::mutex>{ _mutex };
  auto& [_, queues, cmdbuf_pool, working_cmdbufs] = _cmd_contexts.at(family);
  result.queue = queues[queue_index];
  result.borrowed_fence = addWorkingFence();
  result.borrowed_semas = addWorkingSemas(signal_n);
  auto& borrowed_fence = result.borrowed_fence;
  auto& borrowed_semas = result.borrowed_semas;

  auto wait_semas_ =
    wait_infos | views::transform([](auto& info) { return info.waitable.consume().get().get(); }) |
    ranges::to<std::vector>();
  auto signal_semas_ = borrowed_semas |
                       views::transform([](auto& borrowed) { return borrowed.get().get(); }) |
                       ranges::to<std::vector>();
  auto cmdbuf = addWorkingCmdbuf(
    cmdbuf_pool,
    working_cmdbufs,
    &borrowed_fence.get(),
    std::move(wait_semas_),
    std::move(signal_semas_)
  );
  result.cmdbuf = cmdbuf;
  // retrieve back moved resource from working_info
  auto& working = working_cmdbufs.at(cmdbuf);
  auto& wait_semas = working.wait_semas;
  result.signal_semas = working.signal_semas;

  for (auto sema : wait_semas) {
    _working_semas.at(sema).wait_cmd_done = false;
  }
  auto wait_stages = wait_infos | views::transform([](auto info) { return info.stage_mask; });
  result.wait_infos =
    views::zip(wait_semas, wait_stages) |
    views::transform([](auto pair) { return WaitSemaphore{ pair.first, pair.second }; }) |
    ranges::to<std::vector>();
  return result;
}

} // namespace rd::vk