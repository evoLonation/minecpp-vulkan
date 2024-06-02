import toy;
// import application;
import std;

import "vulkan_config.h";
import render.vk.instance;
import render.vk.device;
import render.vk.surface;
import render.vk.swapchain;
import render.vk.executor;
import render.sampler;
import glfw;

int main() {
  try {
    json::test_json();
    toy::test_EnumerateAdaptor();
    toy::test_SortedRange();
    toy::test_ChunkBy();
    toy::test_AnyView();
    toy::test_Generator::test();
    toy::test_EnumSet::test();

    auto window = glfw::Window{ 1920, 1080, "hello vulkan" };

    auto instance = rd::vk::Instance{ "hello" };

    auto surface = rd::vk::Surface{};

    auto device = rd::vk::Device{};

    auto swapchain = rd::vk::Swapchain{};

    auto executor = rd::vk::CommandExecutor{};

    auto sampler = rd::SampledTexture{ "textures/texture.jpg", true };
    // trans::test_trans();
    // auto app = app::Application{};
    // app.runLoop();
  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
