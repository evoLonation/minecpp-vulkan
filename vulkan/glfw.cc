module vulkan.glfw;

import "vulkan_config.h";
import std;
import toy;

namespace vk {

void checkGlfwError() {
  const char* description;

  auto errors =
    views::repeat(description) | views::take_while([&description](auto _) {
      return glfwGetError(&description) != GLFW_NO_ERROR;
    });
  if (!errors.empty()) {
    toy::throwf("{::}", errors);
  }
}

auto createWindow(uint32_t width, uint32_t height, std::string_view title)
  -> GLFWwindow* {
  try {
    if (glfwInit() != GLFW_TRUE) {
      throw std::runtime_error("glfw init failed");
    }
    // 不要创建openGL上下文
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // 禁用改变窗口尺寸
    // glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    auto p_window =
      glfwCreateWindow(width, height, title.data(), nullptr, nullptr);

    checkGlfwError();
    return p_window;
  } catch (const std::exception&) {
    glfwTerminate();
    throw;
  }
}

void destroyWindow(GLFWwindow* p_window) noexcept {
  if (p_window != nullptr) {
    glfwDestroyWindow(p_window);
  }
  glfwTerminate();
}

} // namespace vk