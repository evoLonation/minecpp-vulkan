#ifndef VULKAN_CONFIG_H
#define VULKAN_CONFIG_H

#define VK_USE_PLATFORM_WIN32_KHR
// 太6了，windows的头文件中竟然直接定义了 min 和 max 宏！！！！！！
#define NOMINMAX
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#endif // VULKAN_CONFIG_H
