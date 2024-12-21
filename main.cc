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
// import editor.transform.move;
// import pipeline.uniform;
import engine.asset;
import engine.asset.registry;
import engine.object;
// import engine.mesh;
// import engine.texture;
// import render;
import engine.shape;
import engine.camera;
// import drag;
import editor.transform;

int main() {
  try {
    auto  ctx = ctx::Context{ "hello vulkan", 1920, 1080 };
    auto  dset_pools = rd::meta::DescriptorPools{};
    auto& input = fw::InputProcessor::getInstance();
    auto  draw_unit_observable = eg::DrawUnitObservable{};
    auto  render_pass = eg::RenderPassLoop{};

    auto click_observer = eg::ObjectClickObserver{};
    auto manager = eg::AssetManager{ eg::default_asset_map };

    auto camera = eg::Camera{};
    auto controller = eg::CameraController{ &camera };
    auto panel = eg::TransformControllerPanel{};

    // auto node = eg::DoTheImportThing("model/backpack/backpack.obj");
    // manager.save(node, "backpack");
    auto node = manager.load<eg::SceneObject>("backpack");
    // auto object = std::make_shared<eg::SceneObject>();
    // object->setDrawUnit(
    //   std::make_unique<eg::DrawUnit>(eg::generateCube(), glm::vec3{ 0.5, 0.5, 0.5 })
    // );
    // object.getController().refLocation() = glm::vec3{ 0, 0, 0 };
    // object.getController().refRotateEuler() = glm::vec3{ 0, 0, 0 };
    // object.getController().refScaleFactor() = glm::vec3{ 1, 1, 1 };
    // fw::Loop::getInstance().setMaxFPS(5);

    fw::Loop::getInstance().run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
