import "vulkan_config.h";
import "glfw_config.h";
import "imgui_config.h";

import std;
import toy;
import vulkan;
import glfw;
import glm;
import model;
import render;
import gui;
import control;

int main() {
  try {
    toy::test_EnumerateAdaptor();
    toy::test_SortedRange();
    toy::test_ChunkBy();
    toy::test_CommonView();
    auto applicationName = "hello, vulkan!";
    auto width = 1920;
    auto height = 1080;
    auto ctx = render::Context{ applicationName, width, height };
    ctx.addKeyDownHandler(GLFW_KEY_ESCAPE, [&]() { ctx.setCursorVisible(!ctx.isCursorVisible()); });

    auto pipeline = render::Pipeline{ "hello.vert",
                                      "hello.frag",
                                      model::Vertex::getVertexInfo(),
                                      std::array{ render::ResourceType::UNIFORM,
                                                  render::ResourceType::UNIFORM,
                                                  render::ResourceType::UNIFORM,
                                                  render::ResourceType::SAMPLER } };
    auto [vertex_data, vertex_indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buffer = render::VertexBuffer{ std::span<const model::Vertex>{ vertex_data } };
    auto index_buffer = render::IndexBuffer{ vertex_indices };
    auto sampled_texture = render::SampledTexture{ "model/viking_room.png" };

    auto view = glm::mat4{};
    auto proj = glm::mat4{};

    auto unit_count = 1;
    auto model_datas = std::vector<glm::mat4>(unit_count);
    auto uniforms = std::vector<render::Uniform<glm::mat4>>{};
    uniforms.reserve(unit_count + 2);
    uniforms.emplace_back(view);
    uniforms.emplace_back(proj);
    auto draw_units = std::vector<render::DrawUnit>{};
    for (auto& uniform_data : model_datas) {
      uniforms.emplace_back(uniform_data);
      draw_units.emplace_back(
        pipeline,
        vertex_buffer,
        index_buffer,
        std::array<render::DescriptorResource*, 4>{
          &uniforms[0], &uniforms[1], &uniforms.back(), &sampled_texture }
      );
    }
    auto resource_register = render::ResourceRegister{ std::array<render::DeviceLocalResource*, 3>{
      &vertex_buffer, &index_buffer, &sampled_texture } };
    auto drawer = render::Drawer{ pipeline };
    auto gui_ctx = gui::Context{ drawer };
    drawer.registerUniform(uniforms[0]);
    drawer.registerUniform(uniforms[1]);
    for (auto [unit, uniform] : views::zip(draw_units, uniforms | views::drop(2))) {
      drawer.registerUnit(unit);
      drawer.registerUniform(uniform);
    }
    auto controller = control::model::Controller{ model_datas[0] };
    auto camera_controller = control::camera::Controller{ view, proj };
    camera_controller.setInput();
    // controller.setInput();

    while (!glfwWindowShouldClose(ctx.window)) {
      ctx.processInput();
      gui_ctx.draw([&]() {
        controller.show();
        camera_controller.show();
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
