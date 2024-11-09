module;
#include <vulkan_tool.h>
module render.presentation;

import <vulkan_config.h>;

import render.device;
import render.tool;
import render.tracker;
import render.reflections;
import render.image;

namespace rd {

Presentation::Presentation(VkSurfaceKHR surface) {
  _surface = surface;
  _present_executor = &ExecutorManager::getInstance()[FamilyType::PRESENT];
  if (!recreate()) {
    toy::debugf("create swapchain failed when construct presentation");
  }
}

Presentation::~Presentation() {
  if (_swapchain.isValid()) {
    ImageContext::destroy(std::move(_image_ctxs), _swapchain.get());
  }
}

auto Presentation::acquireNextImage() -> std::pair<uint32, VkResult> {
  uint32 image_index;
  auto   result = vkAcquireNextImageKHR(
    Device::getInstance(),
    _swapchain,
    max_timeout,
    _acquire_ctx.available_sema,
    _acquire_ctx.available_fence,
    &image_index
  );
  CHECK_VK_RESULT(result, { VK_SUCCESS, VK_ERROR_OUT_OF_DATE_KHR, VK_SUBOPTIMAL_KHR });
  if (result != VK_SUCCESS) {
    toy::debugf("acquire next image return: {}", refl::result(result));
  }
  return { image_index, result };
}

auto Presentation::prepare() -> std::optional<Context> {
  if (_need_recreate || !_swapchain.isValid()) {
    return std::nullopt;
  }
  if (_acquire_ctx.fence_waitable) {
    _acquire_ctx.available_fence.wait(true);
    _acquire_ctx.fence_waitable = false;
  }
  auto [image_index, result] = acquireNextImage();
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    _need_recreate = true;
    return std::nullopt;
  }
  // success call
  _acquire_ctx.fence_waitable = true;
  _image_ctxs[image_index].need_release = true;
  auto& ctx = _image_ctxs[image_index];
  auto  image = _swapchain.getImages()[image_index];
  auto  image_view = _swapchain.getImageViews()[image_index].get();

  auto previous_layout = ctx.getTracker().getNowLayout();
  // submit barrier(s) to wait _acquire_ctx.available_sema
  // toy::debugf({}, "prepare(): will call syncScope");
  auto sync = ctx.getTracker().syncScope(
    Scope{ .stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT },
    _present_executor->getFamily(),
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  );
  if (auto* sync_ctx = std::get_if<BarrierRecorder>(&sync)) {
    auto batch = RawWaitCommandBatch{
      .recorder = std::move(*sync_ctx),
      .waits = { { _acquire_ctx.available_sema, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
    };
    _present_executor->submit(batch);
  } else if (auto* sync_ctx = std::get_if<FamilyTransferRecorder>(&sync)) {
    auto waitable = sync_ctx->executeRelease(RawWaitCommandBatch{
      .waits = { { _acquire_ctx.available_sema, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
    });
    _present_executor->submit(sync_ctx->toAcquireBatch(&waitable));
  }
  if (result == VK_SUCCESS) {
    return Context{
      .image_index = image_index,
      .image_manager = &ctx,
    };
  } else {
    _need_recreate = true;
    return std::nullopt;
  }
}

auto Presentation::vkPresent(uint32 image_index, VkSemaphore wait_sema, VkFence signal_fence)
  -> VkResult {
  auto fence_info = VkSwapchainPresentFenceInfoEXT{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
    .swapchainCount = 1,
    .pFences = &signal_fence,
  };
  auto swapchain = _swapchain.get();
  auto present_info = VkPresentInfoKHR{
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .pNext = &fence_info,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &wait_sema,
    .swapchainCount = 1,
    .pSwapchains = &swapchain,
    .pImageIndices = &image_index,
  };
  auto result = vkQueuePresentKHR(
    ExecutorManager::getInstance()[FamilyType::PRESENT].getQueue(), &present_info
  );
  CHECK_VK_RESULT(result, { VK_SUCCESS, VK_ERROR_OUT_OF_DATE_KHR, VK_SUBOPTIMAL_KHR });
  if (result != VK_SUCCESS) {
    toy::debugf("present return: {}", refl::result(result));
  }
  return result;
}

auto Presentation::present(uint32 image_index) -> bool {
  auto& ctx = _image_ctxs[image_index];
  auto  wait_sema = ctx.present_wait_sema.get();
  auto  signal_fence = ctx.present_signal_fence.get();
  if (ctx.fence_waitable) {
    ctx.present_signal_fence.wait(true);
    ctx.fence_waitable = false;
  }
  auto sync = ctx.getTracker().syncScope(
    Scope{ .stage_mask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT },
    _present_executor->getFamily(),
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
  );
  if (auto* sync_ctx = std::get_if<BarrierRecorder>(&sync)) {
    auto batch = RawSignalCommandBatch{
      .recorder = std::move(*sync_ctx),
      .signals = { { wait_sema, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
    };
    _present_executor->submit(batch);
  } else if (auto* sync_ctx = std::get_if<FamilyTransferRecorder>(&sync)) {
    auto waitable = sync_ctx->executeRelease();
    auto acquire_batch = RawSignalCommandBatch{
      .signals = { { wait_sema, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
    };
    sync_ctx->fillAcquireBatch(acquire_batch, &waitable);
    _present_executor->submit(acquire_batch);
  }
  auto result = vkPresent(image_index, wait_sema, signal_fence);
  // sema and fence is wait and signal in all result
  ctx.need_release = false;
  ctx.fence_waitable = true;
  if (result != VK_SUCCESS) {
    _need_recreate = true;
    return false;
  }
  return true;
}

auto Presentation::recreate() -> bool {
  if (_swapchain.isValid()) {
    ImageContext::destroy(std::move(_image_ctxs), _swapchain);
  }
  auto capabilities = VkSurfaceCapabilitiesKHR{};
  CHECK_VK_RESULT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    Device::getInstance().getPdevice().get(), _surface, &capabilities
  ));
  _swapchain = { _surface, capabilities, _swapchain.get() };
  if (!_swapchain.isValid()) {
    return false;
  }
  for (auto [image, image_view] : views::zip(_swapchain.getImages(), _swapchain.getImageViews())) {
    _image_ctxs.push_back(ImageContext{
      image,
      image_view,
      _swapchain.getExtent(),
      _swapchain.getFormat(),
    });
  }
  _need_recreate = false;
  return true;
}

Presentation::ImageContext::ImageContext(
  VkImage image, VkImageView image_view, VkExtent2D extent, VkFormat format
)
  : FrameImageManager{ image,
                       image_view,
                       extent,
                       format,
                       VK_SAMPLE_COUNT_1_BIT,
                       getSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, MipRange{ 0, 1 }) },
    present_wait_sema(createSemaphore()), present_signal_fence{ false }, fence_waitable(false),
    need_release(false) {}

void Presentation::ImageContext::destroy(
  std::vector<ImageContext> image_ctxs, VkSwapchainKHR swapchain
) {
  auto need_release = std::vector<uint32>{};
  for (auto [index, ctx] : image_ctxs | toy::enumerate) {
    if (ctx.need_release) {
      need_release.push_back(index);
    }
    if (ctx.fence_waitable) {
      ctx.present_signal_fence.wait(false);
    }
    // destroy the manager part
    auto fordestroy_ = FrameImageManager{ std::move(ctx) };
  }
  if (!need_release.empty()) {
    auto release_info = VkReleaseSwapchainImagesInfoEXT{
      .sType = VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT,
      .swapchain = swapchain,
      .imageIndexCount = static_cast<uint32>(need_release.size()),
      .pImageIndices = need_release.data(),
    };
    CHECK_VK_RESULT(vkReleaseSwapchainImagesEXT(Device::getInstance(), &release_info));
  }
  image_ctxs.clear();
}
} // namespace rd