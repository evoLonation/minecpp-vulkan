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

} // namespace rd
