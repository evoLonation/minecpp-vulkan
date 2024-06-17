module render.vk.buffer;

import render.vk.device;

namespace rd::vk {

auto createBuffer(VkDeviceSize buffer_size, VkBufferUsageFlags usage) -> rs::Buffer {
  auto buffer_info = VkBufferCreateInfo{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = buffer_size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  return { Device::getInstance(), buffer_info };
}

Buffer::Buffer(
  VkDeviceSize buffer_size, VkBufferUsageFlags usage, VkMemoryPropertyFlags property_flags
) {
  rs::Buffer::operator=(createBuffer(buffer_size, usage));
  _memory = { get(), property_flags };
}

} // namespace rd::vk