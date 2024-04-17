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

using namespace vk;
using namespace glfw;

struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

auto getUniform() {
  static auto startTime = std::chrono::high_resolution_clock::now();

  auto  width = 800;
  auto  height = 600;
  auto  currentTime = std::chrono::high_resolution_clock::now();
  float time =
    std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
  auto ubo = UniformBufferObject{
    .model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
    .view = glm::lookAt(
      glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)
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
    auto applicationName = "hello, vulkan!";
    auto width = 800;
    auto height = 600;
    auto ctx = render::Context{ applicationName, width, height };

    auto pipeline = render::Pipeline{ "hello.vert",
                                      "hello.frag",
                                      model::Vertex::getVertexInfo(),
                                      std::array{ render::ResourceType::UNIFORM,
                                                  render::ResourceType::SAMPLER } };
    auto [vertex_data, vertex_indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buffer = render::VertexBuffer{ std::span<const model::Vertex>{ vertex_data } };
    auto index_buffer = render::IndexBuffer{ vertex_indices };
    auto uniform_data = UniformBufferObject{};
    auto uniform = render::Uniform{ uniform_data };
    auto sampled_texture = render::SampledTexture{ "model/viking_room.png" };
    auto draw_unit = render::DrawUnit{ pipeline,
                                       vertex_buffer,
                                       index_buffer,
                                       std::array<const render::DescriptorResource*, 2>{
                                         &uniform, &sampled_texture } };
    auto resource_register =
      render::ResourceRegister{ std::array<const render::DeviceLocalResource*, 3>{
        &vertex_buffer, &index_buffer, &sampled_texture } };
    auto drawer = render::Drawer{ pipeline };
    drawer.registerUnit(draw_unit);
    drawer.registerUniform(uniform);

    auto input_processor = input::InputProcessor{ ctx.window };
    input_processor.addKeyDownHandler(GLFW_KEY_A, []() { toy::debug("press down A"); });
    input_processor.addKeyHoldHandler(GLFW_KEY_A, [](int time) {
      toy::debugf("press hold A {}", time);
    });
    input_processor.addKeyReleaseHandler(GLFW_KEY_A, [](int time) {
      toy::debugf("press release A when hold {}", time);
    });
    while (!glfwWindowShouldClose(ctx.window)) {
      uniform_data = getUniform();
      input_processor.processInput();
      drawer.draw();
    }

  } catch (const std::exception& e) {

    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
