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
import pipeline.basic;
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
    auto  render_pass = pl::BasicPipeline{};

    auto camera = camera::Camera{};
    auto controller = camera::Controller{ &camera.getView() };

    auto texture = rd::SampledTexture::create(
      "model/viking_room.png", true, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    auto uniform = pl::UniformBuffer<rd::align::mat4>{ { trans::model::create() } };
    auto [vertexes, indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buf = rd::VertexBuffer{ vertexes };
    auto index_buf = rd::IndexBuffer{ indices };
    auto draw_unit = pl::BasicTextureUnit{
      &vertex_buf,
      &index_buf,
      { &uniform, &texture },
    };
    auto cube = tool::Axis{glm::vec3{ 0.0f, 0.0f, 1.0f }};

    render_pass.setCamera(&camera.getData());
    toy::debugf("???");

    loop::Loop::getInstance().run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
