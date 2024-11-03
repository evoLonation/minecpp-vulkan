module render.vk.buffer;

import render.vk.executor;
import render.vk.sync;

namespace rd::vk {

auto createBuffer(VkDeviceSize buffer_size, VkBufferUsageFlags usage) -> rs::Buffer {
  auto buffer_info = VkBufferCreateInfo{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = buffer_size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  return { buffer_info };
}

DeviceLocalBuffer::DeviceLocalBuffer(
  std::span<std::byte const> buffer_data, VkBufferUsageFlags usage
): vk::Buffer{
    buffer_data.size(),
    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
  }, _staging_buffer{buffer_data} {

  auto& copy_executor = vk::CommandExecutorManager::getInstance()[vk::FamilyType::TRANSFER];
  copy_executor.submit([&](VkCommandBuffer cmdbuf) {
    vk::recordCopyBuffer(cmdbuf, _staging_buffer, *this, buffer_data.size());
  });
  getTracker().setNewScope(
    vk::Scope{
      .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
    },
    copy_executor.getFamily()
  );
}

void recordCopyBuffer(
  VkCommandBuffer transfer_cmdbuf,
  VkBuffer        src_buffer,
  VkBuffer        dst_buffer,
  VkDeviceSize    buffer_size
) {
  auto copy_info = VkBufferCopy{
    // this offset is about buffer, not about memory
    .srcOffset = 0,
    .dstOffset = 0,
    .size = buffer_size,
  };
  vkCmdCopyBuffer(transfer_cmdbuf, src_buffer, dst_buffer, 1, &copy_info);
}

} // namespace rd::vk