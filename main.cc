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

int main() {
  try {
    json::test_json();
    toy::test_Generator::test();
    toy::test_EnumSet::test();
    trans::test_trans();
    auto  ctx = ctx::Context{ "hello vulkan", 1920, 1080 };
    auto& input = input::InputProcessor::getInstance();
    auto& gui_ctx = gui::Context::getInstance();

    auto loop = loop::Loop{};
    auto dset_pools = pipeline::DescriptorPools{};
    auto pipeline = pipeline::Pipeline{};
    auto camera = pipeline::Camera{};
    auto draw_unit = pipeline::DrawUnit{};
    pipeline.setCamera(&camera);
    pipeline.addDrawUnit(&draw_unit);
    loop.run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
