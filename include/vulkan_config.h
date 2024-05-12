#define VK_USE_PLATFORM_WIN32_KHR
// 太6了，windows的头文件中竟然直接定义了 min 和 max 宏！！！！！！
#define NOMINMAX
#include "vulkan/vulkan.h"
#include "vulkan/vulkan_core.h"
// windows头文件中定义的逆天宏
#undef near
#undef far
#undef DELETE
#undef interface