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
  VkBufferUsageFlags usage, vk::Scope dst_scope, std::span<const std::byte> buffer_data
) {
  auto& ctx = vk::Device::getInstance();

  auto buffer_size = (uint32)buffer_data.size();
  _staging_buffer = { buffer_data };

  vk::Buffer::operator=({
    buffer_size,
    usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
  });
  _tracker = { get() };

  auto& copy_executor = vk::CommandExecutorManager::getInstance()[vk::FamilyType::TRANSFER];
  auto& graphcis_executor = vk::CommandExecutorManager::getInstance()[vk::FamilyType::GRAPHICS];

  auto barrier = _tracker.syncScope(
    vk::Scope{
      .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
    },
    copy_executor.getFamily()
  );
  toy::checkf(std::get_if<std::monostate>(&barrier), "setNewScope failed");
  barrier = _tracker.syncScope(dst_scope, graphcis_executor.getFamily());
  auto& [release, acquire, _] = std::get<vk::FamilyTransferRecorder>(barrier);

  auto copy_recorder = [&](VkCommandBuffer cmdbuf) {
    vk::recordCopyBuffer(cmdbuf, _staging_buffer, *this, buffer_size);
    release(cmdbuf);
  };
  auto waitable = copy_executor.submit(copy_recorder);
  graphcis_executor.submit(vk::CommandBatch{
    .recorder = std::move(acquire),
    .waits = { { &waitable, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
  });
}

VertexBuffer::VertexBuffer(std::span<const std::byte> vertex_data, VertexInfo vertex_info)
  : DeviceLocalBuffer(
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      vk::Scope{
        .stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        .access_mask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
      },
      vertex_data
    ),
    _vertex_info(vertex_info) {}

IndexBuffer::IndexBuffer(std::span<const uint16_t> indices)
  : DeviceLocalBuffer(
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      vk::Scope{
        .stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        .access_mask = VK_ACCESS_INDEX_READ_BIT,
      },
      std::as_bytes(indices)
    ),
    _index_number(indices.size()) {}

} // namespace rd
