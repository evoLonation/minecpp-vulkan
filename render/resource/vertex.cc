module render.vertex;

import render.executor;

namespace rd {

auto operator==(const VertexInfo& a, const VertexInfo& b) -> bool {
  return a.binding_description == b.binding_description &&
         a.attribute_descriptions.begin() == b.attribute_descriptions.begin() &&
         a.attribute_descriptions.end() == b.attribute_descriptions.end();
}

auto device_checkers::vertex(DeviceCapabilityBuilder& builder) -> bool {
  auto formats =
    FormatTypeInfos::applyFunc([]<typename... Info> { return std::array{ Info::format... }; });
  toy::debugf("the vertex formats: {::}", formats);
  if (!builder.enableFeature(&VkPhysicalDeviceFeatures::shaderFloat64)) {
    return false;
  }
  return builder.getPdevice().checkFormatSupport(
    FormatTarget::BUFFER, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT, formats
  );
}

} // namespace rd
