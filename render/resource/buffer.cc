module render.buffer;

import render.executor;
import render.sync;

namespace rd {

void copyBuffer(
  VkCommandBuffer transfer_cmdbuf,
  VkBuffer        src_buffer,
  VkBuffer        dst_buffer,
  VkDeviceSize    src_offset,
  VkDeviceSize    dst_offset,
  VkDeviceSize    buffer_size
) {
  auto copy_info = VkBufferCopy{
    // this offset is about buffer, not about memory
    .srcOffset = src_offset,
    .dstOffset = dst_offset,
    .size = buffer_size,
  };
  vkCmdCopyBuffer(transfer_cmdbuf, src_buffer, dst_buffer, 1, &copy_info);
}

auto createBuffer(VkDeviceSize buffer_size, VkBufferUsageFlags usage) -> rs::Buffer {
  auto buffer_info = VkBufferCreateInfo{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .size = buffer_size,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  return { buffer_info };
}

auto Buffer::operator=(Buffer&& e) noexcept -> Buffer& {
  _size = e._size;
  _usage = e._usage;
  _tracker = std::move(e._tracker);
  _memory = std::move(e._memory);
  rs::Buffer::operator=(std::move(e));
  return *this;
}

auto HostBuffer::operator=(HostBuffer&& e) noexcept -> HostBuffer& {
  _memory = std::move(e._memory);
  Buffer::operator=(std::move(e));
  return *this;
}

DeviceLocalBuffer::DeviceLocalBuffer(
  std::span<std::byte const> buffer_data, VkBufferUsageFlags usage
): Buffer{
    buffer_data.size(),
    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
  }, _staging_buffer{buffer_data, VK_BUFFER_USAGE_TRANSFER_SRC_BIT} {

  auto& copy_executor = ExecutorManager::getInstance()[FamilyType::TRANSFER];
  copy_executor.submit([&](VkCommandBuffer cmdbuf) {
    copyBuffer(cmdbuf, _staging_buffer, *this, 0, 0, buffer_data.size());
  });
  getTracker().setNewScope(
    Scope{
      .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
    },
    copy_executor.getFamily()
  );
}

} // namespace rd