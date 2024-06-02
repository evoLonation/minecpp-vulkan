module render.presentation;

import "vulkan_config.h";

import std;
import toy;

namespace rd {

class Presentation: public toy::ProactiveSingleton<Presentation> {
public:
  auto needRecreate() -> bool;
  auto getCurrentImage() -> VkImage;
  void submitPresent(VkSemaphore wait_sema, VkSemaphore available_sema) {
    auto present_info = VkPresentInfoKHR{
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = wait_sema,
      .swapchainCount = 1,
      .pSwapchains = &Swapchain::getInstance().get(),
      .pImageIndices = &_current_index,
      // .pResults: 当有多个 swapchain 时检查每个的result
    };
    if (auto result =
          vkQueuePresentKHR(present_executor[_in_flight_index].getQueue(), &present_info);
        result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
      toy::debugf(
        "the queue present return {}",
        result == VK_ERROR_OUT_OF_DATE_KHR ? "out of date error" : "sub optimal"
      );
      _last_present_failed = true;
    } else {
      vk::checkVkResult(result, "present");
    }
    if (auto result = vkAcquireNextImageKHR(
          ctx.device,
          ctx.swapchain,
          std::numeric_limits<uint64_t>::max(),
          worker.image_available_sema,
          VK_NULL_HANDLE,
          &image_index
        );
        result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || _last_present_failed) {
      if (ctx.tryRecreate()) {
        _last_present_failed = false;
        for (auto usage : _image_useds) {
          usage = false;
        }
        draw();
        return;
      } else {
        // toy::debugf("recreate swapchain failed, draw frame return");
        return;
      }
    } else {
      vk::checkVkResult(result, "acquire next image");
    }
  }

private:
  uint32_t _current_index;
};

} // namespace rd