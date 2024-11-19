#include <toy.h>
import toy;
// import application;
import std;

import <vulkan_config.h>;
import <glfw_config.h>;
import render;
import context;
import glm;
import input;
import model;
import glfw;
import transform;
import gui;
import loop;
// import pipeline.outline;
// import pipeline.basic;
import pipeline.light;
import pipeline.uniform;
import render;
// import pipeline.resources;
// import pipeline.drawunit;
import tool.move;
import camera;
import drag;

int main() {
  try {
    json::test_json();
    toy::test_Generator::test();
    toy::test_EnumSet::test();
    trans::test_trans();
    auto  ctx = ctx::Context{ "hello vulkan", 1920, 1080 };
    auto  dset_pools = rd::meta::DescriptorPools{};
    auto& input = input::InputProcessor::getInstance();
    auto  render_pass = pl::LightPipeline{};

    auto camera = camera::Camera{};
    auto controller = camera::Controller{ &camera };

    auto texture = rd::SampledTexture::create(
      "model/viking_room.png", true, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    auto uniform_1 = pl::UniformBuffer{ pl::LightModelVertexData{
      .model = trans::model::create(),
      .normal_model = glm::mat3{ trans::model::create() },
    } };
    auto uniform_2 = pl::UniformBuffer{ pl::LightModelFragmentData{
      .shininess = 32,
    } };
    auto uniform_3 = pl::UniformBuffer{ pl::LightData{
      .direction = glm::vec3{ 0.0f, 0.0f, -1.0f },
      .ambient = glm::vec3{ 0.1f, 0.1f, 0.1f },
      .diffuse = glm::vec3{ 0.5f, 0.5f, 0.5f },
      .specular = glm::vec3{ 1.0f, 1.0f, 1.0f },
      .view_pos = glm::vec3{ 5.0f, 5.0f, 5.0f },
    } };
    auto action = loop::ActionHandler{
      [&](loop::ActionContext const& ctx) { uniform_3->view_pos = camera.getViewPos(); },
      loop::Stage::BEFORE_RENDER,
    };
    auto global_dset = rd::meta::DescriptorSet<pl::layout_global_light>{ &uniform_3 };
    render_pass.setGlobalDescriptorSet(&global_dset);
    auto [positions, normals, tex_coords, indices] = model::getModelInfo("model/viking_room.obj");
    auto vertices = std::vector<pl::LightVertex>{};
    for (auto [pos, norm, tex] : views::zip(positions, normals, tex_coords)) {
      vertices.emplace_back(pos, norm, tex);
    }
    auto vertex_buf = rd::VertexBuffer{ vertices };
    auto index_buf = rd::IndexBuffer{ indices };
    auto draw_unit = pl::LightUnit{
      &vertex_buf,
      &index_buf,
      { &uniform_1, &uniform_2, &texture, &texture },
    };
    // auto cube = tool::Axis{ glm::vec3{ 0.0f, 0.0f, 1.0f } };
    // auto sphere = tool::Arrow{ glm::vec3{ 0.0f, 0.0f, 1.0f } };

    render_pass.setCamera(&camera.getData());

    loop::Loop::getInstance().run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
