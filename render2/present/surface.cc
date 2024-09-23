module render.vk.surface;

import glfw;
import "glfw_config.h";

namespace rd::vk {

Surface::Surface() {
  auto create_info = VkWin32SurfaceCreateInfoKHR{
    .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
    .hinstance = GetModuleHandle(nullptr),
    .hwnd = glfwGetWin32Window(glfw::Window::getInstance().get()),
  };
  rs::Surface::operator=(create_info);
}

} // namespace rd::vk