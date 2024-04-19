import "vulkan_config.h";
import "glfw_config.h";

import std;
import toy;
import vulkan;
import glfw;
import glm;
import model;
import input;
import render;
import gui;

using namespace vk;
using namespace glfw;

struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

auto getUniform(int index) {
  static auto startTime = std::chrono::high_resolution_clock::now();

  auto  width = 1920;
  auto  height = 1080;
  auto  currentTime = std::chrono::high_resolution_clock::now();
  float time =
    std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
  auto ubo = UniformBufferObject{
    .model = glm::rotate(
      glm::translate(glm::mat4(1.0f), glm::vec3{ index, 0.0f, 0.0f }),
      time * glm::radians(90.0f),
      glm::vec3(0.0f, 0.0f, 1.0f)
    ),
    .view = glm::lookAt(
      glm::vec3(5.0f, 5.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)
    ),
    .proj = glm::perspective(glm::radians(45.0f), width * 1.0f / height, 0.1f, 10.0f),
  };
  ubo.proj[1][1] *= -1;
  return ubo;
}

int main() {
  // model::getModelInfo("model/viking_room.obj");
  // return 0;
  try {
    toy::test_EnumerateAdaptor();
    toy::test_SortedRange();
    toy::test_ChunkBy();
    toy::test_CommonView();
    auto applicationName = "hello, vulkan!";
    auto width = 1920;
    auto height = 1080;
    auto ctx = render::Context{ applicationName, width, height };
    auto input_processor = input::InputProcessor{ ctx.window };

    auto pipeline = render::Pipeline{ "hello.vert",
                                      "hello.frag",
                                      model::Vertex::getVertexInfo(),
                                      std::array{ render::ResourceType::UNIFORM,
                                                  render::ResourceType::SAMPLER } };
    auto [vertex_data, vertex_indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buffer = render::VertexBuffer{ std::span<const model::Vertex>{ vertex_data } };
    auto index_buffer = render::IndexBuffer{ vertex_indices };
    auto sampled_texture = render::SampledTexture{ "model/viking_room.png" };

    int  unit_count = 8;
    auto uniform_datas = std::vector<UniformBufferObject>(unit_count);
    auto uniforms = std::vector<render::Uniform<UniformBufferObject>>{};
    uniforms.reserve(unit_count);
    auto draw_units = std::vector<render::DrawUnit>{};
    for (auto& uniform_data : uniform_datas) {
      uniforms.emplace_back(uniform_data);
      draw_units.emplace_back(
        pipeline,
        vertex_buffer,
        index_buffer,
        std::array<render::DescriptorResource*, 2>{ &uniforms.back(), &sampled_texture }
      );
    }
    auto resource_register = render::ResourceRegister{ std::array<render::DeviceLocalResource*, 3>{
      &vertex_buffer, &index_buffer, &sampled_texture } };
    auto drawer = render::Drawer{ pipeline };
    auto gui_ctx = gui::Context{ drawer };
    for (auto [unit, uniform] : views::zip(draw_units, uniforms)) {
      drawer.registerUnit(unit);
      drawer.registerUniform(uniform);
    }

    input_processor.addKeyDownHandler(GLFW_KEY_A, []() { toy::debug("press down A"); });
    input_processor.addKeyHoldHandler(GLFW_KEY_A, [](int time) {
      toy::debugf("press hold A {}", time);
    });
    input_processor.addKeyReleaseHandler(GLFW_KEY_A, [](int time) {
      toy::debugf("press release A when hold {}", time);
    });
    while (!glfwWindowShouldClose(ctx.window)) {
      for (auto i : views::iota(0, unit_count)) {
        uniform_datas[i] = getUniform(i - 4);
      }
      input_processor.processInput();
      gui::draw([]() {
        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You
        // can browse its code to learn more about Dear ImGui!).
        ImGui::ShowDemoWindow();
        ImGui::Begin("Hello, world!");
        static auto f = 0.0f;
        ImGui::SliderFloat(
          "float", &f, 0.0f, 1.0f
        ); // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::End();
      });
      drawer.draw();
    }
    drawer.waitIdle();
  } catch (const std::exception& e) {

    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
