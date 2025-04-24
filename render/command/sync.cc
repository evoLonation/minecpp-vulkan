module;
#include <platform.h>
#include <toy.h>
#include <vulkan_tool.h>
module render.sync;

import <vulkan_config.h>;
import render.tool;

namespace rd {

auto device_checkers::sync(DeviceCapabilityBuilder& builder) -> std::expected<void, std::string> {
  if constexpr (PLATFORM_VULKAN_VERSION >= VK_API_VERSION_1_3) {
    if (!builder.enableFeature(&VkPhysicalDeviceVulkan13Features::synchronization2)) {
      return std::unexpected{ "synchronization2 not supported" };
    }
  } else {
    if (!builder.enableExtension(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) ||
        !builder.enableFeature<
          VkPhysicalDeviceSynchronization2Features,
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES>(
          { &VkPhysicalDeviceSynchronization2Features::synchronization2 }
        )) {
      return std::unexpected{ "synchronization2 extension not supported" };
    }
  }
  if (!builder.enableFeature(&VkPhysicalDeviceVulkan12Features::timelineSemaphore)) {
    return std::unexpected{ "timelineSemaphore not supported" };
  }
  return {};
}

TimelineSemaphore::TimelineSemaphore(uint64 initial_value) {
  auto type_info = VkSemaphoreTypeCreateInfo{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    .initialValue = initial_value,
  };
  VkSemaphoreCreateInfo create_info{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = &type_info,
  };
  rs::Semaphore::operator=(create_info);
  _host_signal_value = initial_value;
  _device_signal_value = initial_value;
}

auto TimelineSemaphore::wait(uint64 value, uint64 nano_timeout) -> bool {
  return wait(std::array{ std::pair{ this, value } }, false, nano_timeout);
}

auto TimelineSemaphore::wait(
  std::span<std::pair<TimelineSemaphore*, uint64> const> semaphores, bool any, uint64 nano_timeout
) -> bool {
  if (semaphores.empty()) {
    return true;
  }
  auto handles = semaphores | views::transform([](auto& s) { return s.first->get(); }) |
                 ranges::to<std::vector>();
  auto values =
    semaphores | views::transform([](auto& s) { return s.second; }) | ranges::to<std::vector>();
  auto wait_info = VkSemaphoreWaitInfo{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    .flags = any ? VK_SEMAPHORE_WAIT_ANY_BIT : 0u,
    .semaphoreCount = static_cast<uint32>(semaphores.size()),
    .pSemaphores = handles.data(),
    .pValues = values.data(),
  };
  auto res = CHECK_VK_RESULT(
    vkWaitSemaphores(Device::getInstance(), &wait_info, nano_timeout), { VK_SUCCESS, VK_TIMEOUT }
  );
  return res == VK_SUCCESS;
}

void TimelineSemaphore::signal(uint64 value) {
  auto signal_info = VkSemaphoreSignalInfo{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
    .semaphore = get(),
    .value = value,
  };
  CHECK_VK_RESULT(vkSignalSemaphore(Device::getInstance(), &signal_info));
  _host_signal_value = value;
}

auto TimelineSemaphore::getCurrentValue() const -> uint64 {
  uint64 value;
  CHECK_VK_RESULT(vkGetSemaphoreCounterValue(Device::getInstance().get(), get(), &value));
  TOY_ASSERT(value <= getBiggestSignalValue());
  return value;
}

/**
 * @brief must call it when submit device signal operation
 */
void TimelineSemaphore::pendingDeviceSignal(uint64 value) {
  TOY_ASSERT(value > _host_signal_value);
  if (value > _device_signal_value) {
    _device_signal_value = value;
  }
  _device_signal_value = value;
}

auto TimelineSemaphore::getBiggestSignalValue() const -> uint64 {
  return std::max(_device_signal_value, _host_signal_value);
}

auto TimelineSemaphore::waitIdle(
  std::span<TimelineSemaphore* const> semaphores, bool any, uint64 nano_timeout
) -> bool {
  return wait(
    semaphores |
      views::transform([](auto& s) { return std::pair{ s, s->getBiggestSignalValue() }; }) |
      ranges::to<std::vector>(),
    any,
    nano_timeout
  );
}

auto TimelineSemaphore::waitIdle(uint64 nano_timeout) -> bool {
  return wait(getBiggestSignalValue(), nano_timeout);
}

auto IncrementalSemaphore::getDeviceSyncInfo() -> std::pair<VkSemaphore, uint64> {
  return { _sema.get(), _sema.getBiggestSignalValue() };
}

auto IncrementalSemaphore::wait(uint64 nano_timeout) -> bool {
  return _sema.waitIdle(nano_timeout);
}

auto IncrementalSemaphore::wait(
  std::span<IncrementalSemaphore* const> semaphores, bool any, uint64 nano_timeout
) -> bool {
  return TimelineSemaphore::waitIdle(
    semaphores | views::transform([](auto& s) { return &s->_sema; }) | ranges::to<std::vector>(),
    any,
    nano_timeout
  );
}

void IncrementalSemaphore::pendingNewSignal() {
  TOY_ASSERT(wait(0));
  TOY_ASSERT(_sema.getCurrentValue() == _sema.getBiggestSignalValue());
  _sema.pendingDeviceSignal(_sema.getBiggestSignalValue() + 1);
}

auto IncrementalSemaphore::valid() const -> bool { return _sema.get(); }

TimelineSemaphorePool::TimelineSemaphorePool() { _idle_semas.resize(10); }

void TimelineSemaphorePool::recycle(IncrementalSemaphore s) {
  _working_semas.push_back(std::move(s));
}

void TimelineSemaphorePool::expand() { _idle_semas.push_back(IncrementalSemaphore{}); }

auto TimelineSemaphorePool::isEmpty() -> bool { return _idle_semas.empty(); }

auto TimelineSemaphorePool::extract() -> IncrementalSemaphore {
  auto ret = std::move(_idle_semas.back());
  ret.pendingNewSignal();
  _idle_semas.pop_back();
  return ret;
}

void TimelineSemaphorePool::tryShrink() {
  auto shrink_size = 5;
  toy::debugf("shrink {} -> {}", _idle_semas.size(), shrink_size);
  if (_idle_semas.size() <= shrink_size) {
    return;
  }
  _idle_semas.erase(_idle_semas.begin() + shrink_size, _idle_semas.end());
}

void TimelineSemaphorePool::workingToIdle() {
  for (auto iter = _working_semas.begin(); iter != _working_semas.end();) {
    if (iter->wait(0)) {
      _idle_semas.push_back(std::move(*iter));
      iter = _working_semas.erase(iter);
    } else {
      iter++;
    }
  }
}

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

void Fence::wait(bool reset, uint64 timeout) {
  auto handle = get();
  CHECK_VK_RESULT(vkWaitForFences(Device::getInstance(), 1, &handle, VK_TRUE, timeout));
  if (reset) {
    this->reset();
  }
}

void Fence::reset() {
  auto handle = get();
  CHECK_VK_RESULT(vkResetFences(Device::getInstance(), 1, &handle));
}

auto Fence::isSignaled() -> bool {
  /**
   * VK_SUCCESS: The fence specified by fence is signaled.
   * VK_NOT_READY: The fence specified by fence is unsignaled.
   * VK_ERROR_DEVICE_LOST: The device has been lost. See Lost Device.
   */
  auto result =
    CHECK_VK_RESULT(vkGetFenceStatus(Device::getInstance(), get()), { VK_SUCCESS, VK_NOT_READY });
  return result == VK_SUCCESS;
}

auto scope2str(Scope scope) -> std::string {
  return "{" + refl::flags<VkPipelineStageFlagBits2>(scope.stage_mask) + " / " +
         refl::flags<VkAccessFlagBits2>(scope.access_mask) + "}";
}

void recordPipelineBarrier(
  VkCommandBuffer                         cmdbuf,
  std::span<const VkMemoryBarrier2>       memory_barriers,
  std::span<const VkBufferMemoryBarrier2> buffer_barriers,
  std::span<const VkImageMemoryBarrier2>  image_barriers
) {
  auto dependency_info = VkDependencyInfo{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    //  VK_DEPENDENCY_BY_REGION_BIT:
    //  实现可以分区域进行同步(前面写一部分，后面就可以先读一部分)
    .dependencyFlags = 0,
    .memoryBarrierCount = static_cast<uint32>(memory_barriers.size()),
    .pMemoryBarriers = memory_barriers.data(),
    .bufferMemoryBarrierCount = static_cast<uint32>(buffer_barriers.size()),
    .pBufferMemoryBarriers = buffer_barriers.data(),
    .imageMemoryBarrierCount = static_cast<uint32>(image_barriers.size()),
    .pImageMemoryBarriers = image_barriers.data(),
  };
  vkCmdPipelineBarrier2KHR(cmdbuf, &dependency_info);
};

void recordBufferBarrier(
  VkCommandBuffer    cmdbuf,
  VkBuffer           buffer,
  BarrierScope       barrier_scope,
  FamilyTransferInfo family_info
) {
  auto [src_scope, dst_scope] = barrier_scope;
  auto buffer_barrier = VkBufferMemoryBarrier2{
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = src_scope.stage_mask,
    .srcAccessMask = src_scope.access_mask,
    .dstStageMask = dst_scope.stage_mask,
    .dstAccessMask = dst_scope.access_mask,
    .srcQueueFamilyIndex = family_info.src_family,
    .dstQueueFamilyIndex = family_info.dst_family,
    .buffer = buffer,
    .offset = 0,
    .size = VK_WHOLE_SIZE,
  };
  recordPipelineBarrier(cmdbuf, {}, { &buffer_barrier, 1 }, {});
}

void recordImageBarrier(
  VkCommandBuffer         cmdbuf,
  VkImage                 image,
  VkImageSubresourceRange subresource_range,
  LayoutTransitionInfo    layout_info,
  BarrierScope            barrier_scope,
  FamilyTransferInfo      family_info
) {
  if constexpr (false) {
    toy::debugf(
      {},
      "record image barrier:\n  image = {},\n  layout = {} -> {},\n  scope = {} -> {},\n  family = "
      "{} -> {}",
      reinterpret_cast<void*>(image),
      layout_info.src_layout,
      layout_info.dst_layout,
      barrier_scope.src_scope,
      barrier_scope.dst_scope,
      family_info.src_family,
      family_info.dst_family
    );
  }
  auto [src_scope, dst_scope] = barrier_scope;
  auto image_barrier = VkImageMemoryBarrier2{
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    .pNext = nullptr,
    .srcStageMask = src_scope.stage_mask,
    .srcAccessMask = src_scope.access_mask,
    .dstStageMask = dst_scope.stage_mask,
    .dstAccessMask = dst_scope.access_mask,
    .oldLayout = layout_info.src_layout,
    .newLayout = layout_info.dst_layout,
    .srcQueueFamilyIndex = family_info.src_family,
    .dstQueueFamilyIndex = family_info.dst_family,
    .image = image,
    .subresourceRange = subresource_range,
  };
  recordPipelineBarrier(cmdbuf, {}, {}, { &image_barrier, 1 });
}

} // namespace rd