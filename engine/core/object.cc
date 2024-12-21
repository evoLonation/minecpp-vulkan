module;
#include <toy.h>
module engine.object;

namespace eg {

SceneObject::~SceneObject() {
  if (_draw_unit.get()) {
    _draw_unit2object.erase(_draw_unit.get());
  }
}

void SceneObject::setDrawUnit(uptr<DrawUnit> draw_unit) {
  if (_draw_unit.get()) {
    _draw_unit2object.erase(_draw_unit.get());
  }
  _draw_unit = std::move(draw_unit);
  setActive(true);
  if (_draw_unit.get()) {
    _draw_unit2object[_draw_unit.get()] = this;
  }
}

void SceneObject::setActive(bool active) {
  if (active) {
    _draw_unit->setActive(&getTransToWorld());
  } else {
    _draw_unit->resetActive();
  }
}

auto SceneObject::isSelected() -> bool {
  return _draw_unit.get() && IdPipeline::getInstance().getSelectedDrawUnit() == _draw_unit.get();
}

auto SceneObject::getSelected() -> SceneObject* {
  if (auto iter = _draw_unit2object.find(IdPipeline::getInstance().getSelectedDrawUnit());
      iter != _draw_unit2object.end()) {
    return iter->second;
  }
  return nullptr;
}

auto SceneObject::assetSerialize() -> AssetPackager {
  if (!_draw_unit) {
    return { 0, {}, Node::assetSerialize() };
  } else {
    return {
      1,
      std::tuple{
        AssetRef{ _draw_unit->getMesh().getPtr() },
        AssetRef{ _draw_unit->getTexture().getPtr() },
        _draw_unit->isUpLayer(),
      },
      Node::assetSerialize(),
    };
  }
}

void SceneObject::assetDeserialize(AssetUnpacker unpacker) {
  Node::assetDeserialize(unpacker.extractInherited());
  auto id = unpacker.getId();
  if (id == 0) {
    unpacker.extract<>();
  } else {
    auto [mesh_ref, texture_ref, up_layer] = unpacker.extract<AssetRef, AssetRef, bool>();
    setDrawUnit(
      std::make_unique<DrawUnit>(mesh_ref.consume<Mesh>(), texture_ref.consume<Texture>(), up_layer)
    );
  }
}

FixedDistanceNode::FixedDistanceNode() {
  _fixed_distance_node = std::make_shared<Node>();
  _view_pos = &Camera::getInstance().getViewPos();
  // todo: make _distance reactive too
  _fixed_distance_node->attachTo(this);
  _fixed_distance_node->getTransToParent().bindNow(
    [this](glm::mat4 const& to_world, glm::vec3 const& location_camera) {
      {
        auto location_object = mt::transformDot(to_world, glm::vec3{ 0, 0, 0 });
        auto new_location_object =
          location_camera + _distance * glm::normalize(location_object - location_camera);
        return mt::inverse(to_world) *
               mt::translate(glm::vec3{ new_location_object - location_object }) * to_world;
      }
    },
    &getTransToWorld(),
    _view_pos
  );
}

} // namespace eg