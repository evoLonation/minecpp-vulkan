module render.context;

import render.vk.queue_requestor;
import render.sampler;

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
  };
  _device.reset(new vk::Device{ device_checkers, _instance->instance });
  _swapchain.reset(new vk::Swapchain{ *_surface, *_device });
  _command_executor.reset(new vk::CommandExecutor{
    *_device, queue_requestor.getFamilyQueueCounts(*_device) });
  {
    using namespace vk::executors;
    using QueueExecutor = vk::CommandExecutor::QueueExecutor;
    graphics = QueueExecutor{ 0, { 0, 1 } };
    present = QueueExecutor{ 1, { 0, 1 } };
    copy = QueueExecutor{ 2, { 0, 1 } };
  }
}

} // namespace rd
