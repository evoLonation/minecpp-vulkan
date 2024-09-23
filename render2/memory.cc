module render.vk.memory;

import std;
import toy;

import "vulkan_config.h";
import render.vk.tool;
import render.vk.resource;
import render.vk.device;

namespace rd::vk {

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

  if (_memory_properties.get() == nullptr) {
    _memory_properties.reset(new VkPhysicalDeviceMemoryProperties{});
    vkGetPhysicalDeviceMemoryProperties(device.pdevice(), _memory_properties.get());
  }
  uint32_t memory_type_index;
  if (auto optional = toy::findIf(
        std::span(_memory_properties->memoryTypes, _memory_properties->memoryTypeCount) |
          toy::enumerate,
        [requirements, property_flags](auto pair) {
          auto [i, memory_type] = pair;
          return (requirements.memoryTypeBits & (1 << i)) &&
                 (memory_type.propertyFlags & property_flags) == property_flags;
        }
      );
      optional.has_value()) {
    memory_type_index = optional->first;
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

HostVisibleMemory::HostVisibleMemory(HostVisibleMemory&& e) noexcept {
  _data = e._data;
  _memory = e._memory;
  e._data = nullptr;
}

auto HostVisibleMemory::operator=(HostVisibleMemory&& e) noexcept -> HostVisibleMemory& {
  unmap();
  _data = e._data;
  _memory = e._memory;
  e._data = nullptr;
  return *this;
}

auto HostVisibleMemory::data() -> void* {
  if (_data == nullptr) {
    checkVkResult(
      vkMapMemory(Device::getInstance(), _memory, 0, VK_WHOLE_SIZE, 0, &_data), "map memory"
    );
  }
  return _data;
}
void HostVisibleMemory::fill(std::span<const std::byte> buffer_data) {
  auto buffer_size = buffer_data.size();
  std::copy(buffer_data.begin(), buffer_data.end(), reinterpret_cast<std::byte*>(data()));
}

void HostVisibleMemory::unmap() {
  if (_data != nullptr) {
    vkUnmapMemory(Device::getInstance(), _memory);
  }
}

} // namespace rd::vk