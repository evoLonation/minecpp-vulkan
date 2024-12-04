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
import gui.context;
import framework;
// import pipeline.outline;
import pipeline.basic;
import pipeline.light;
import pipeline.uniform;
import engine.object;
import engine.mesh;
import engine.texture;
import render;
// import component;
// import pipeline.resources;
// import pipeline.drawunit;
// import tool.move;
// import tool.rotate;
import tool.shape;
import camera;
import drag;
// import editor.transform;

int main() {
  try {
    json::test_json();
    toy::test_Generator::test();
    test_EnumSet::test();
    auto  ctx = ctx::Context{ "hello vulkan", 1920, 1080 };
    auto  dset_pools = rd::meta::DescriptorPools{};
    auto& input = fw::InputProcessor::getInstance();
    // auto  transform_gui = eg::ModelTransformGuiController{};
    auto render_pass = eg::LightPipeline{};
    // auto  click_observer = eg::ObjectClickObserver{};
    // auto  move_manager = eg::MoveControllerManager{};
    // auto rotate_manager = eg::RotateControllerManager{};

    auto camera = eg::Camera{};
    auto controller = eg::CameraController{ &camera };
    render_pass.setCamera(&camera.getData());

    auto [positions, normals, tex_coords, indices] = model::getModelInfo("model/viking_room.obj");
    auto mesh = std::make_shared<eg::Mesh>(eg::MeshData{
      std::move(positions), std::move(normals), std::move(tex_coords), std::move(indices) });
    auto texture = std::make_shared<eg::Texture>(eg::Texture{ rd::SampledTexture::fromFile(
      "model/viking_room.png", true, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    ) });
    auto object1 = std::make_shared<eg::SceneObject>(
      std::make_unique<eg::LightObject>(std::move(mesh), std::move(texture))
    );
    auto cylinder = std::make_shared<eg::SceneObject>(eg::createShape(
      eg::generateCylinder(1, 2, 77), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 1.0f }
    ));
    cylinder->getTransform().location = { 0, 3, 0 };
    auto cube = std::make_shared<eg::SceneObject>(
      eg::createShape(eg::generateCube(), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 1.0f })
    );
    // auto selector_cube = eg::MoveTransformSelector{ &cube };

    // auto cylinder = std::make_unique<tool::SceneComponent>(tool::createShape(
    //   tool::generateCylinder(), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 10.0f }
    // ));
    // auto sphere = std::make_unique<tool::SceneComponent>(tool::createShape(
    //   tool::generateSphere(), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 15.0f }
    // ));
    // auto axis = std::make_unique<tool::SceneComponent>(tool::createShape(
    //   tool::generateAxis(), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 0.0f }
    // ));
    // auto move = eg::MoveController{};

    // // auto selector_cone = eg::MoveTransformSelector{};
    // auto cone = eg::SceneComponent{};
    // {
    //   auto cone_ = eg::SceneComponent{ eg::createShape(
    //     eg::generateCone(), glm::vec3{ 0.3, 0.5, 0.1 }, glm::vec3{ 0.0f, 0.0f, 5.0f }
    //   ) };
    //   // selector_cone = { &cone_ };
    //   cone_.attachTo(&cube);
    //   cube.getTransform().translate(glm::vec3{ 0.0f, 0.0f, 1.0f });
    //   // move = tool::MoveController{ &cube };
    //   cone = std::move(cone_);
    // }

    fw::Loop::getInstance().run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
