module render.vk.sync;

import "vulkan_config.h";
import render.vk.tool;
import render.vk.device;

namespace rd::vk {

Semaphore::Semaphore(bool init) {
  VkSemaphoreCreateInfo create_info{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  };
  rs::Semaphore::operator=({ Device::getInstance(), create_info });
}

Fence::Fence(bool signaled) {
  VkFenceCreateInfo create_info{
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
  };
  if (signaled) {
    create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  }
  rs::Fence::operator=({ Device::getInstance(), create_info });
}

void Fence::wait(bool reset) {
  checkVkResult(
    vkWaitForFences(
      Device::getInstance(), 1, &get(), VK_TRUE, std::numeric_limits<uint64_t>::max()
    ),
    "wait fences"
  );
  if (reset) {
    this->reset();
  }
}

void Fence::reset() {
  checkVkResult(vkResetFences(Device::getInstance(), 1, &get()), "reset fence");
}

auto Fence::isSignaled() -> bool {
  /**
   * VK_SUCCESS: The fence specified by fence is signaled.
   * VK_NOT_READY: The fence specified by fence is unsignaled.
   * VK_ERROR_DEVICE_LOST: The device has been lost. See Lost Device.
   */
  auto result = vkGetFenceStatus(Device::getInstance(), get());
  if (result == VK_SUCCESS) {
    return true;
  } else if (result == VK_NOT_READY) {
    return false;
  } else {
    vk::checkVkResult(result, "get fence status");
  }
  std::unreachable();
}

} // namespace rd::vk