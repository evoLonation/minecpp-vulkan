module context;

import render.queue_requestor;
import render.sync;
import render.sampler;
import render.vertex;
import render.presentation;
import render.swapchain;
import render.alignment;

import <vulkan_config.h>;

namespace ctx {

Context::Context(const std::string& app_name, uint32 width, uint32 height) {
  using std::make_unique;
  _glfw_ctx = make_unique<glfw::Context>();
  _glfw_window = make_unique<glfw::Window>(width, height, app_name);
  auto instance_extensions = std::vector<std::string>{};
  instance_extensions.append_range(rd::extensions::surface);
  _instance =
    std::make_unique<rd::InstanceResource>(rd::createInstance(app_name, instance_extensions));
  _surface = std::make_unique<rd::rs::Surface>(rd::createSurface(*_glfw_window));
  using namespace std::placeholders;
  auto queue_requestor = rd::QueueRequestor{ _surface->get() };
  auto device_checkers = std::vector<rd::DeviceCapabilityChecker>{
    [&](auto& ctx) { return queue_requestor.checkPdevice(ctx); },
    std::bind(rd::Swapchain::checkPdevice, _surface->get(), _1),
    rd::SampledTexture::checkPdevice,
    rd::device_checkers::vertex,
    rd::device_checkers::sync,
    rd::device_checkers::attachment,
    rd::align::device_checkers::alignment,
  };
  _device = std::make_unique<rd::Device>(rd::Device::create(device_checkers));
  _executor_manager = queue_requestor.createExecutorManager();
  _image_context = std::make_unique<rd::ImageContext>();
  _framebuffer_pool = std::make_unique<rd::FramebufferPool>();
  _presentation = std::make_unique<rd::Presentation>(_surface->get());
  _input_processor = std::make_unique<input::InputProcessor>();
  _loop = std::make_unique<loop::Loop>();
  _gui_ctx = std::make_unique<gui::Context>();
}

} // namespace ctx
