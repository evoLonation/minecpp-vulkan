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
import pipeline.outline;
import pipeline.basic;
import pipeline.resources;
import pipeline.drawunit;
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
    auto  dset_pools = pl::DescriptorPools{};
    auto& input = input::InputProcessor::getInstance();

    auto pipeline = pl::BasicPipeline{};
    auto camera = camera::Camera{};
    auto controller = camera::Controller{ &camera.getView() };
    auto draw_unit_0 = pl::DrawUnit{ 1 };
    auto draw_unit_1 = pl::DrawUnit{ 2, glm::vec3{ 1.0f, 1.0f, 1.0f } };
    auto draw_unit_2 = pl::DrawUnit{ 3, glm::vec3{ -1.0f, -1.0f, -1.0f } };
    // auto cube_0 = tool::Cube{ glm::vec3{ 1.0f, 1.0f, 1.0f } };
    // auto cone_0 = tool::Cone{ glm::vec3{ 0.0f, 0.0f, 1.0f } };
    auto axis_0 = tool::Axis{ glm::vec3{ 1.0f, 0.0f, 1.0f } };
    auto dragger = drag::CursorDragger{
      &camera.getView(),
      &draw_unit_1.getModelTrans().get().model,
    };
    pipeline.setCamera(&camera.getData());
    pipeline.addDrawUnit(&draw_unit_0);
    pipeline.addDrawUnit(&draw_unit_1);
    pipeline.addDrawUnit(&draw_unit_2);
    // pipeline.addDrawUnit(&cube_0);
    // pipeline.addDrawUnit(&cone_0);
    pipeline.addDrawUnit(&axis_0);
    
    loop::Loop::getInstance().run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
