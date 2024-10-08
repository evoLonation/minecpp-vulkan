module render.vk.presentation;

import "vulkan_config.h";
import render.vk.swapchain;
import render.vk.executor;
import render.vk.sync;
import render.vk.image;

namespace rd::vk {

auto Presentation::prepare() -> std::optional<PresentContext> {
  bool recreated = _present_recreated;
  if (!_swapchain->valid()) {
    _swapchain->updateCapabilities();
    if (_swapchain->needRecreate()) {
      _swapchain->recreate();
      recreated = true;
    }
  }
  if (!_swapchain->valid()) {
    return std::nullopt;
  }
  if (recreated) {
    auto images = _swapchain->images();
    for (auto image : images) {
      _image_resources[image].tracker = ImageBarrierTracker{
        image,
        getSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, vk::MipRange{ 0, 1 }),
      };
    }
  }
  auto image_available_sema = _swapchain->getImageAvailableSema();
  auto image_index = _swapchain->getCurrentImageIndex();
  auto image = _swapchain->images()[image_index];

  return PresentContext{
    .wait_sema = image_available_sema,
    .image_index = image_index,
    .need_recreate = recreated,
    .tracker = _image_resources[image].tracker,
  };
}

void Presentation::present() {
  // transfer image ownership from graphics to present
  // transfer image layout from <custom> to presentable
  auto  image_index = _swapchain->getCurrentImageIndex();
  auto& present_executor = CommandExecutorManager::getInstance()[FamilyType::PRESENT];
  auto  image = _swapchain->images()[image_index];
  auto& tracker = _image_resources[image].tracker;
  auto& present_wait_sema = _image_resources[image].present_wait_sema;
  auto  sync = tracker.syncScope(
    Scope{ .stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT },
    present_executor.getFamily(),
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  );
  if (auto* barrier = std::get_if<BarrierRecorder>(&sync)) {
    auto batch = RawSignalCommandBatch{
      .recorder = std::move(*barrier),
      .signals = { { present_wait_sema, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
    };
    present_executor.submit(batch);
  } else if (auto* barrier = std::get_if<FamilyTransferRecorder>(&sync)) {
    auto  transfer_sema = _image_resources[image].present_transfer_sema.get();
    auto& release_executor = CommandExecutorManager::getInstance()[barrier->release_family];
    auto  release_batch = CommandBatch{ .recorder = std::move(barrier->release) };
    auto  waitable = release_executor.submit(release_batch);
    auto  acquire_batch = RawSignalCommandBatch{
       .recorder = std::move(barrier->acquire),
       .waits = { { &waitable, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
       .signals = { { present_wait_sema, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
    };
    present_executor.submit(acquire_batch);
  } else {
    auto recorder = [&](VkCommandBuffer cmdbuf) {
      recordImageBarrier(
        cmdbuf,
        image,
        getSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, vk::MipRange{ 0, 1 }),
        { VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR },
        { Scope{ .stage_mask = VK_PIPELINE_STAGE_NONE },
          Scope{ .stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
        {}
      );
    };
    auto batch = RawSignalCommandBatch{
      .recorder = recorder,
      .signals = { { present_wait_sema, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
    };
    present_executor.submit(batch);
  }
  if (!_swapchain->present(present_wait_sema, present_executor.getQueue())) {
    _swapchain->updateCapabilities();
    _swapchain->recreate();
    _present_recreated = true;
  } else {
    _present_recreated = false;
  }
}

} // namespace rd::vk