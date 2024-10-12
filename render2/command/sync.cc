module render.vk.sync;

import "vulkan_config.h";
import render.vk.tool;
import render.vk.device;

namespace rd::vk {

auto createSemaphore() -> rs::Semaphore {
  return rs::Semaphore{ VkSemaphoreCreateInfo{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  } };
}

Fence::Fence(bool signaled) {
  VkFenceCreateInfo create_info{
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
  };
  if (signaled) {
    create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  }
  rs::Fence::operator=(create_info);
}

void Fence::wait(bool reset, uint64_t timeout) {
  auto handle = get();
  checkVkResult(
    vkWaitForFences(Device::getInstance(), 1, &handle, VK_TRUE, timeout), "wait fences"
  );
  if (reset) {
    this->reset();
  }
}

void Fence::reset() {
  auto handle = get();
  checkVkResult(vkResetFences(Device::getInstance(), 1, &handle), "reset fence");
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