module;
#include <toy.h>
#include <vulkan_tool.h>

module render.memory;

import <vulkan_config.h>;
import render.tool;
import render.device;

namespace rd {

Memory::Memory(VkBuffer buffer, VkMemoryPropertyFlags property_flags)
  : Memory(
      [buffer]() {
        /*
         * alignment: The offset in bytes where the buffer begins in the allocated
         *  region of memory, depends on bufferInfo.usage and bufferInfo.flags.
         * memoryTypeBits: Bit field of the memory types that are suitable for the
         *  buffer.
         */
        VkMemoryRequirements memory_requirements;
        vkGetBufferMemoryRequirements(Device::getInstance(), buffer, &memory_requirements);
        return memory_requirements;
      }(),
      property_flags
    ) {
  vkBindBufferMemory(Device::getInstance(), buffer, get(), 0);
}
Memory::Memory(VkImage image, VkMemoryPropertyFlags property_flags)
  : Memory(
      [image]() {
        VkMemoryRequirements memory_requirements;
        vkGetImageMemoryRequirements(Device::getInstance(), image, &memory_requirements);
        return memory_requirements;
      }(),
      property_flags
    ) {
  vkBindImageMemory(Device::getInstance(), image, get(), 0);
}
Memory::Memory(VkMemoryRequirements requirements, VkMemoryPropertyFlags property_flags) {
  auto& device = Device::getInstance();

  auto   memory_properties = device.getPdevice().getMemoryProperties();
  uint32 memory_type_index;
  if (auto optional = toy::findIf(
        std::span(memory_properties.memoryTypes, memory_properties.memoryTypeCount) |
          toy::enumerate,
        [requirements, property_flags](auto pair) {
          auto [i, memory_type] = pair;
          return (requirements.memoryTypeBits & (1 << i)) &&
                 (memory_type.propertyFlags & property_flags) == property_flags;
        }
      )) {
    memory_type_index = std::get<0>(*optional);
  } else {
    toy::throwf("can not find suitable memory type");
  }
  auto allocate_info = VkMemoryAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .allocationSize = requirements.size,
    .memoryTypeIndex = memory_type_index,
  };
  rs::Memory::operator=(allocate_info);
}

auto HostMemoryManager::data() -> std::span<std::byte> {
  if (!_data.get()) {
    void* data;
    CHECK_VK_RESULT(vkMapMemory(Device::getInstance(), _memory.get(), 0, VK_WHOLE_SIZE, 0, &data));
    _data.reset(data);
  }
  return std::span<std::byte>{ reinterpret_cast<std::byte*>(_data.get()), _size };
}
void HostMemoryManager::fill(std::span<const std::byte> buffer_data) {
  TOY_ASSERT(buffer_data.size() <= _size);
  std::copy(buffer_data.begin(), buffer_data.end(), data().begin());
}

void HostMemoryManager::beforeDestroy_() {
  if (_data.get()) {
    vkUnmapMemory(Device::getInstance(), _memory.get());
  }
}

} // namespace rd