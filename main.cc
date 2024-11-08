#include <toy.h>
import toy;
// import application;
import std;

import <vulkan_config.h>;
import <glfw_config.h>;
import render.instance;
import render.device;
import render.surface;
import render.resource;
import render.executor;
import render.queue_requestor;
import render.image;
import render.render_pass;
import render.descriptor;
import render.buffer;
import render.framebuffer;
import render.presentation;
import context;
import render.sync;
import render.tracker;
import render.sampler;
import render.vertex;
import glm;
import input;
import model;
import glfw;
import transform;
import gui;
import loop;
import pipeline;
import camera;

int main() {
  try {
    json::test_json();
    toy::test_Generator::test();
    toy::test_EnumSet::test();
    trans::test_trans();
    auto  ctx = ctx::Context{ "hello vulkan", 1920, 1080 };
    auto  dset_pools = pipeline::DescriptorPools{};
    auto& input = input::InputProcessor::getInstance();

    auto pipeline = pipeline::OutlinePipeline{};
    auto camera = pipeline::Camera{};
    auto controller = camera::Controller{ &camera.getView() };
    auto draw_unit_0 = pipeline::DrawUnit{};
    auto draw_unit_1 = pipeline::DrawUnit{ glm::vec3{ 1.0f, 1.0f, 1.0f } };
    auto draw_unit_2 = pipeline::DrawUnit{ glm::vec3{ -1.0f, -1.0f, -1.0f } };
    pipeline.setCamera(&camera);
    pipeline.addDrawUnit(&draw_unit_0);
    pipeline.addDrawUnit(&draw_unit_1);
    pipeline.addDrawUnit(&draw_unit_2);
    loop::Loop::getInstance().run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
