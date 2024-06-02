module render.vk.command;

import "vulkan_config.h";
import render.vk.tool;
import render.vk.resource;
import render.vk.device;

import std;
import toy;

namespace rd::vk {

auto createCommandPool(uint32_t family_index, bool short_live) -> rs::CommandPool {
  auto pool_create_info = VkCommandPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：允许重置单个command
    // buffer，否则就要重置命令池里的所有buffer
    // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT: 命令缓冲区会很频繁的记录新命令
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = family_index,
  };
  if (short_live) {
    pool_create_info.flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  }
  return { Device::getInstance(), pool_create_info };
}

auto allocateCommandBuffers(VkCommandPool command_pool, uint32_t count) -> rs::CommandBuffers {
  auto cbuffer_alloc_info = VkCommandBufferAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = command_pool,
    // VK_COMMAND_BUFFER_LEVEL_PRIMARY: 主缓冲区，类似于main
    // VK_COMMAND_BUFFER_LEVEL_SECONDARY: 次缓冲区，可复用，类似于其他函数
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = count,
  };
  return { Device::getInstance(), cbuffer_alloc_info };
}

} // namespace rd::vk