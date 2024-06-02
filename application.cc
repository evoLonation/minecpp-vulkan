module application;

import std;
import toy;

import glfw;
import input;

import "glfw_config.h";

namespace app {

void Application::initObjects() {
  initObject<glfw::Window>();
  initObject<input::InputProcessor>();
}

void Application::updateInterval() {
  auto interval = chrono::high_resolution_clock::now() - _last_time;
  _last_time += interval;
  _interval = chrono::duration_cast<chrono::microseconds>(interval).count() / 1'000'000.0f;
}

void Application::runLoop() {
  auto& input_processor = input::InputProcessor::getInstance();
  while (!glfwWindowShouldClose(glfw::Window::getInstance().get())) {
    input_processor.processInput(_interval);
    
  }
}

} // namespace app