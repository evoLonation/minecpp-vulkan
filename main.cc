#include <toy.h>
import toy;
// import application;
import std;

import <vulkan_config.h>;
import <glfw_config.h>;
import render;
import context;
import glm;

import model;
import glfw;
import math;
import gui;
import framework;
// import pipeline.outline;
import pipeline.basic;
import pipeline.light;
import pipeline.uniform;
import render;
// import pipeline.resources;
// import pipeline.drawunit;
import tool.move;
import tool.shape;
import camera;
import drag;

int main() {
  try {
    json::test_json();
    toy::test_Generator::test();
    test_EnumSet::test();
    auto  ctx = ctx::Context{ "hello vulkan", 1920, 1080 };
    auto  dset_pools = rd::meta::DescriptorPools{};
    auto& input = fw::InputProcessor::getInstance();
    auto  transform_gui = tool::ModelTransformGuiController{};
    auto  render_pass = pl::LightPipeline{};

    auto camera = camera::Camera{};
    auto controller = camera::Controller{ &camera };
    render_pass.setCamera(&camera.getData());

    auto [positions, normals, tex_coords, indices] = model::getModelInfo("model/viking_room.obj");
    // auto object1 = tool::SceneComponent{ {
    //   pl::LightMesh{
    //     std::move(positions), std::move(normals), std::move(tex_coords), std::move(indices) },
    //   rd::SampledTexture::fromFile(
    //     "model/viking_room.png", true, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    //   ),
    // } };
    auto cube = tool::SceneComponent{ tool::createShape(
      tool::generateCube(), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 1.0f }
    ) };
    auto selector_cube = tool::MoveTransformSelector{ &cube };

    // auto cylinder = std::make_unique<tool::SceneComponent>(tool::createShape(
    //   tool::generateCylinder(), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 10.0f }
    // ));
    // auto sphere = std::make_unique<tool::SceneComponent>(tool::createShape(
    //   tool::generateSphere(), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 15.0f }
    // ));
    // auto axis = std::make_unique<tool::SceneComponent>(tool::createShape(
    //   tool::generateAxis(), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 0.0f }
    // ));
    auto move = tool::MoveController{};

    auto selector_cone = tool::MoveTransformSelector{};
    {
      auto cone = tool::SceneComponent{ tool::createShape(
        tool::generateCone(), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 5.0f }
      ) };
      selector_cone = { &cone };
      cube.add(std::move(cone));
      cube.getTransToParent().translate(glm::vec3{ 0.0f, 0.0f, 5.0f });
    }

    fw::Loop::getInstance().run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
