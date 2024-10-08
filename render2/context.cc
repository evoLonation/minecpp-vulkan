module render.context;

import render.vk.queue_requestor;
import render.vk.sync;
import render.sampler;
import render.vertex;

import "vulkan_config.h";

namespace rd {

using namespace vk;

auto requestGraphicQueue(const QueueFamilyCheckContext& ctx) -> bool {
  return ctx.properties.queueFlags & VK_QUEUE_GRAPHICS_BIT;
}
auto requestPresentQueue(const QueueFamilyCheckContext& ctx, VkSurfaceKHR surface) -> bool {
  VkBool32 presentSupport = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(ctx.device, ctx.index, surface, &presentSupport);
  return presentSupport == VK_TRUE;
}
auto requestTransferQueue(const QueueFamilyCheckContext& ctx) -> bool {
  // 支持 graphics 和 compute operation 的 queue 也必定支持 transfer operation
  return ctx.properties.queueFlags &
         (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT);
}

Context::Context(const std::string& app_name, uint32 width, uint32 height) {
  _glfw_ctx.reset(new glfw::Context{});
  _glfw_window.reset(new glfw::Window{ width, height, app_name });
  _input_processor.reset(new input::InputProcessor{});

  auto instance_extensions = std::vector<std::string>{};
  instance_extensions.append_range(extensions::surface);
  _instance.reset(new InstanceResource{ createInstance("hello", instance_extensions) });
  _surface.reset(new rs::Surface{ createSurface(*_glfw_window) });
  using namespace std::placeholders;
  auto queue_requirements = std::array{
    QueueFamilyRequirement{ requestGraphicQueue, 1 },
    QueueFamilyRequirement{ std::bind(requestPresentQueue, _1, _surface->get()), 1 },
    QueueFamilyRequirement{ requestTransferQueue, 1 },
  };
  auto queue_requestor = QueueRequestor{ queue_requirements };
  auto device_checkers = std::array{
    DeviceCapabilityChecker{ [&](auto& ctx) { return queue_requestor.checkPdevice(ctx); } },
    DeviceCapabilityChecker{ std::bind(Swapchain::checkPdevice, _surface->get(), _1) },
    DeviceCapabilityChecker{ SampledTexture::checkPdevice },
    DeviceCapabilityChecker{ device_checkers::vertex },
    DeviceCapabilityChecker{ device_checkers::sync },
  };
  _device.reset(new Device{ device_checkers });
  _swapchain.reset(new Swapchain{ *_surface });
  auto family_counts = queue_requestor.getFamilyQueueCounts(*_device);
  auto family_info = std::vector<std::pair<FamilyType, FamilyQueueCount>>(3);
  using enum FamilyType;
  family_info[0] = { GRAPHICS, family_counts[0] };
  family_info[1] = { PRESENT, family_counts[1] };
  family_info[2] = { TRANSFER, family_counts[2] };
  _command_executor_manager.reset(new CommandExecutorManager{ family_info });
}

} // namespace rd
