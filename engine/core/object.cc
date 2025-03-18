module;
#include <toy.h>
module engine.object;

namespace eg {

auto DrawUnitBase::getTransform() -> Reval<glm::mat4> const& {
  TOY_ASSERT(isActive());
  return *_transform;
}

auto DrawUnitBase::getSceneObject() -> SceneObject* {
  TOY_ASSERT(isActive());
  return _scene_object;
}

void DrawUnitBase::setActive(SceneObject* scene_object, Reval<glm::mat4> const* transform) {
  _scene_object = scene_object;
  _transform = transform;
}

void DrawUnitBase::resetActive() {
  _scene_object = nullptr;
  _transform = nullptr;
}

LightUnit::LightUnit(ptr<Mesh> mesh, ptr<Texture> texture, bool up_layer) {
  init(std::move(mesh), std::move(texture), up_layer);
}

LightUnit::LightUnit(MeshData mesh_data, glm::vec3 color, bool up_layer) {
  init(mesh_data, color, up_layer);
}

void LightUnit::init(ptr<Mesh> mesh, ptr<Texture> texture, bool up_layer) {
  _mesh = std::move(mesh);
  _texture = std::move(texture);
  _up_layer = up_layer;
}

void LightUnit::init(MeshData mesh_data, glm::vec3 color, bool up_layer) {
  mesh_data.tex_coords = views::repeat(glm::vec2{ 0, 0 }) |
                         views::take(mesh_data.positions.size()) | ranges::to<std::vector>();
  auto mesh = std::make_shared<Mesh>(mesh_data);
  auto color_a = glm::vec4{ color, 1.0f };
  auto texture = std::make_shared<Texture>(rd::SampledTexture{
    std::as_bytes(std::span{ &color_a, 1 }),
    VK_FORMAT_R32G32B32A32_SFLOAT,
    { 1, 1 },
    false,
    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
  });
  init(std::move(mesh), std::move(texture), up_layer);
}

void LightUnit::assetSerialize(AssetPackager& packager) {
  DrawUnit::assetSerialize(packager);
  toy::debug("LightUnit::assetSerialize");
  packager.packWithSave(_mesh);
  packager.packWithSave(_texture);
  packager.packWithSave(_up_layer);
}

void LightUnit::assetDeserialize(AssetUnpacker& unpacker) {
  DrawUnit::assetDeserialize(unpacker);
  auto [mesh, texture, up_layer] =
    unpacker.unpacks<std::shared_ptr<Mesh>, std::shared_ptr<Texture>, bool>();
  init(std::move(mesh), std::move(texture), up_layer);
}

void SceneObject::setDrawUnit(uptr<DrawUnitBase> draw_unit) {
  if (_draw_unit) {
    setActive(false);
  }
  _draw_unit = std::move(draw_unit);
  TOY_ASSERT(!_draw_unit->isActive());
  setActive(true);
}

void SceneObject::setActive(bool active) {
  // TOY_ASSERT(_draw_unit || !active);
  if (!_draw_unit || active == _draw_unit->isActive()) {
    return;
  }
  if (active) {
    _draw_unit->setActive(this, &getTransToWorld());
  } else {
    _draw_unit->resetActive();
  }
}

auto SceneObject::isActive() -> bool { return _draw_unit && _draw_unit->isActive(); }

void SceneObject::assetSerialize(AssetPackager& packager) {
  Node::assetSerialize(packager);
  auto has_draw_unit = _draw_unit != nullptr;
  packager.pack(has_draw_unit);
  if (has_draw_unit) {
    packager.packWithSave(_draw_unit);
  }
}

void SceneObject::assetDeserialize(AssetUnpacker& unpacker) {
  Node::assetDeserialize(unpacker);
  if (unpacker.unpack<bool>()) {
    auto draw_unit = unpacker.unpack<uptr<DrawUnitBase>>();
    setDrawUnit(std::move(draw_unit));
  }
}

} // namespace eg