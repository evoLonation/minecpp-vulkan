import toy;
// import application;
import std;

import <vulkan_config.h>;
import <glfw_config.h>;
import render;
import context;
import glm;

import engine.importer;
import glfw;
import math;
import gui.context;
import framework;
// import pipeline.basic;
import pipeline.loop;
// import engine.draw_unit;
// import editor.transform.move;
// import pipeline.uniform;
// import engine.assets;
// import engine.assets.registry;
import engine.object;
// import engine.mesh;
// import engine.texture;
// import render;
// import engine.shape;
import engine.camera;
// import drag;
import editor.transform;

int main() {
  try {
    auto  ctx = ctx::Context{ "hello vulkan", 1920, 1080 };
    auto  dset_pools = rd::meta::DescriptorPools{};
    auto& input = fw::InputProcessor::getInstance();
    auto  render_pass = eg::RenderPassLoop{};
    auto  click_observer = eg::ObjectClickObserver{};
    // auto  manager = eg::AssetManager{ eg::default_asset_map };

    auto camera = eg::Camera{};
    auto controller = eg::CameraController{ &camera };
    auto panel = eg::TransformControllerPanel{};

    auto node = eg::DoTheImportThing("model/backpack/backpack.obj");

    fw::Loop::getInstance().run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
