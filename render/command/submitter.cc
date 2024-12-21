module;
#include <toy.h>
module render.submitter;

namespace rd {

void Submitter::addNeedSync(
  ImageBarrierTracker*                   tracker,
  Scope                                  scope,
  VkImageLayout                          layout,
  std::vector<CommandBatch::RawWaitInfo> waits
) {
  auto sync = tracker->syncScope(scope, _executor->getFamily(), layout);
  addNeedSync(sync, std::move(waits));
}

void Submitter::addNeedSync(
  BufferBarrierTracker* tracker, Scope scope, std::vector<CommandBatch::RawWaitInfo> waits
) {
  auto sync = tracker->syncScope(scope, _executor->getFamily());
  addNeedSync(sync, std::move(waits));
}

auto Submitter::submit(
  std::function<void(VkCommandBuffer)> recorder, std::vector<CommandBatch::RawSignalInfo> signals
) -> Waitable {
  auto waitables = std::vector<Waitable>{};
  for (auto& [family, recorder_info] : _release_recorders) {
    auto& executor = ExecutorManager::getInstance()[family];
    auto  waitable = executor.submit(CommandBatch{
       .recorder =
        [&](VkCommandBuffer cmd) {
          for (auto& release : recorder_info.recorders) {
            release(cmd);
          }
        },
       .raw_waits = std::move(recorder_info.waits),
    });
    waitables.push_back(std::move(waitable));
  }
  auto recorder_all = [&](VkCommandBuffer cmd) {
    for (auto& sync : _sync_recorder.recorders) {
      sync(cmd);
    }
    recorder(cmd);
  };
  auto waits = std::vector<CommandBatch::WaitInfo>();
  for (auto& waitable : waitables) {
    waits.push_back({ &waitable, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT });
  }
  auto batch = CommandBatch{
    .recorder = std::move(recorder_all),
    .waits = std::move(waits),
    .raw_waits = std::move(_sync_recorder.waits),
    .raw_signals = std::move(signals),
  };
  return _executor->submit(std::move(batch));
}

void Submitter::addNeedSync(SyncContext& sync, std::vector<CommandBatch::RawWaitInfo> waits) {
  if (auto* recorder = std::get_if<BarrierRecorder>(&sync)) {
    _sync_recorder.recorders.push_back(std::move(*recorder));
    _sync_recorder.waits.append_range(waits);
  } else if (auto* ctx = std::get_if<FamilyTransferRecorder>(&sync)) {
    auto& recorder_info = _release_recorders[ctx->release_family];
    recorder_info.recorders.push_back(std::move(ctx->release));
    recorder_info.waits.append_range(waits);
    _sync_recorder.recorders.push_back(std::move(ctx->acquire));
  }
}

} // namespace rd