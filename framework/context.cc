module context;

import render.queue_requestor;
import render.sync;
import render.sampler;
import render.vertex;
import render.presentation;
import render.swapchain;

import <vulkan_config.h>;

namespace ctx {

Context::Context(const std::string& app_name, uint32 width, uint32 height) {
  using std::make_unique;
  _glfw_ctx = make_unique<glfw::Context>();
  _glfw_window = make_unique<glfw::Window>(width, height, app_name);
  auto instance_extensions = std::vector<std::string>{};
  instance_extensions.append_range(rd::extensions::surface);
  _instance.reset(new rd::InstanceResource{ rd::createInstance("hello", instance_extensions) });
  _surface.reset(new rd::rs::Surface{ rd::createSurface(*_glfw_window) });
  using namespace std::placeholders;
  auto queue_requestor = rd::QueueRequestor{ _surface->get() };
  auto device_checkers = std::vector<rd::DeviceCapabilityChecker>{
    [&](auto& ctx) { return queue_requestor.checkPdevice(ctx); },
    std::bind(rd::Swapchain::checkPdevice, _surface->get(), _1),
    rd::SampledTexture::checkPdevice,
    rd::device_checkers::vertex,
    rd::device_checkers::sync,
  };
  _device.reset(new rd::Device{ rd::Device::create(device_checkers) });
  _executor_manager = queue_requestor.createExecutorManager();
  _image_context.reset(new rd::ImageContext{});
  _framebuffer_pool.reset(new rd::FramebufferPool{});
  _presentation = std::make_unique<rd::Presentation>(_surface->get());
  _input_processor.reset(new input::InputProcessor{});
  _gui_ctx.reset(new gui::Context{});
}

} // namespace ctx
