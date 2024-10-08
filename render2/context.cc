module render.context;

import render.vk.queue_requestor;
import render.vk.sync;
import render.sampler;
import render.vertex;

namespace rd {

Context::Context(const std::string& app_name, uint32 width, uint32 height) {
  _glfw_ctx.reset(new glfw::Context{});
  _glfw_window.reset(new glfw::Window{ width, height, app_name, *_glfw_ctx });
  _input_processor.reset(new input::InputProcessor{ *_glfw_ctx });

  _instance.reset(new vk::InstanceResource{
    vk::createInstance("hello", vk::Surface::getRequiredInstanceExtensions()) });
  _surface.reset(new vk::Surface{ _instance->instance, *_glfw_window });
  auto queue_requirements = std::array{
    vk::QueueFamilyRequirement{ vk::requestGraphicQueue, 1 },
    vk::QueueFamilyRequirement{ vk::requestPresentQueue, 1 },
    vk::QueueFamilyRequirement{ vk::requestTransferQueue, 1 },
  };
  auto queue_requestor = vk::QueueRequestor{ queue_requirements };
  auto device_checkers = std::array{
    vk::DeviceCapabilityChecker{ [&](auto& ctx) { return queue_requestor.checkPdevice(ctx); } },
    vk::DeviceCapabilityChecker{ vk::Swapchain::checkPdevice },
    vk::DeviceCapabilityChecker{ SampledTexture::device_checker },
    vk::DeviceCapabilityChecker{ checkVertexPdeviceSupport },
    vk::DeviceCapabilityChecker{ vk::sync::checkPdevice },
  };
  _device.reset(new vk::Device{ device_checkers, _instance->instance });
  _swapchain.reset(new vk::Swapchain{ *_surface, *_device });
  auto family_counts = queue_requestor.getFamilyQueueCounts(*_device);
  auto family_info = std::vector<std::pair<vk::FamilyType, vk::FamilyQueueCount>>(3);
  using enum vk::FamilyType;
  family_info[0] = { GRAPHICS, family_counts[0] };
  family_info[1] = { PRESENT, family_counts[1] };
  family_info[2] = { TRANSFER, family_counts[2] };
  _command_executor_manager.reset(new vk::CommandExecutorManager{ family_info });
}

} // namespace rd
