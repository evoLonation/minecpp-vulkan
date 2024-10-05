module render.vk.presentation;

import "vulkan_config.h";
import render.vk.swapchain;
import render.vk.executor;
import render.vk.sync;
import render.vk.image;

namespace rd::vk {

auto Presentation::prepare() -> std::optional<PresentContext> {
  auto& swapchain = Swapchain::getInstance();
  bool  recreated = _present_recreated;
  if (!swapchain.valid()) {
    swapchain.updateCapabilities();
    if (swapchain.needRecreate()) {
      swapchain.recreate();
      recreated = true;
    }
  }
  if (!swapchain.valid()) {
    return std::nullopt;
  }
  if (recreated) {
    auto images = swapchain.images();
    for (auto image : images) {
      _image_resources[image].tracker = ImageBarrierTracker{
        image,
        getSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, vk::MipRange{ 0, 1 }),
      };
    }
  }
  auto image_available_sema = swapchain.getImageAvailableSema();
  auto image_index = swapchain.getCurrentImageIndex();
  auto image = swapchain.images()[image_index];

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
  auto& swapchain = Swapchain::getInstance();
  auto  image_index = swapchain.getCurrentImageIndex();
  auto& present_executor = executors::present;
  auto  image = swapchain.images()[image_index];
  auto& tracker = _image_resources[image].tracker;
  auto& present_wait_sema = _image_resources[image].present_wait_sema;
  auto  sync = tracker.syncScope(
    Scope{ .stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT },
    present_executor.getFamily(),
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  );
  if (auto* barrier = std::get_if<BarrierRecorder>(&sync)) {
    present_executor.submit(*barrier, {}, std::array{ present_wait_sema.get() });
  } else if (auto* barrier = std::get_if<FamilyTransferRecorder>(&sync)) {
    auto transfer_sema = _image_resources[image].present_transfer_sema.get();
    executors::getByFamily(barrier->release_family)
      .submit(barrier->release, {}, std::array{ transfer_sema });
    present_executor.submit(
      barrier->acquire,
      std::array{ WaitSemaphore{ transfer_sema, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
      std::array{ present_wait_sema.get() }
    );
  } else {
    present_executor.submit(
      [&](VkCommandBuffer cmdbuf) {
        recordImageBarrier(
          cmdbuf,
          image,
          getSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, vk::MipRange{ 0, 1 }),
          { VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR },
          { Scope{ .stage_mask = VK_PIPELINE_STAGE_NONE },
            Scope{ .stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
          {}
        );
      },
      {},
      std::array{ present_wait_sema.get() }
    );
  }
  if (!swapchain.present(present_wait_sema, present_executor[0].getQueue())) {
    swapchain.updateCapabilities();
    swapchain.recreate();
    _present_recreated = true;
  } else {
    _present_recreated = false;
  }
}

} // namespace rd::vk