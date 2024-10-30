module render.vertex;

import render.vk.executor;

namespace rd {

auto operator==(const VertexInfo& a, const VertexInfo& b) -> bool {
  return a.binding_description == b.binding_description &&
         a.attribute_descriptions.begin() == b.attribute_descriptions.begin() &&
         a.attribute_descriptions.end() == b.attribute_descriptions.end();
}

auto vk::device_checkers::vertex(vk::DeviceCapabilityBuilder& builder) -> bool {
  auto formats =
    FormatTypeInfos::applyFunc([]<typename... Info> { return std::array{ Info::format... }; });
  toy::debugf("the vertex formats: {::}", formats | views::transform([](auto a) {
                                            return static_cast<uint32>(a);
                                          }));
  if (!builder.enableFeature(&VkPhysicalDeviceFeatures::shaderFloat64)) {
    return false;
  }
  return builder.getPdevice().checkFormatSupport(
    vk::FormatTarget::BUFFER, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT, formats
  );
}

DeviceLocalBuffer::DeviceLocalBuffer(
  VkBufferUsageFlags usage, std::span<const std::byte> buffer_data
): vk::Buffer{
    buffer_data.size(),
    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
  }, _staging_buffer{buffer_data} {

  auto& copy_executor = vk::CommandExecutorManager::getInstance()[vk::FamilyType::TRANSFER];

  getTracker().setNewScope(
    vk::Scope{
      .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
    },
    copy_executor.getFamily()
  );

  auto copy_recorder = [&](VkCommandBuffer cmdbuf) {
    vk::recordCopyBuffer(cmdbuf, _staging_buffer, *this, buffer_data.size());
  };
  copy_executor.submit(copy_recorder);
}

VertexBuffer::VertexBuffer(std::span<const std::byte> vertex_data, VertexInfo vertex_info)
  : DeviceLocalBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_data), _vertex_info(vertex_info) {}

IndexBuffer::IndexBuffer(std::span<const uint16_t> indices)
  : DeviceLocalBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, std::as_bytes(indices)),
    _index_number(indices.size()) {}

} // namespace rd
