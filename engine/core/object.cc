module;
#include <toy.h>
module engine.object;

namespace eg {

void SceneObject::setDrawUnit(uptr<DrawUnit> draw_unit) {
  _draw_unit = std::move(draw_unit);
  setActive(true);
}

void SceneObject::setActive(bool active) {
  if (active) {
    _draw_unit->setActive(this, &getTransToWorld());
  } else {
    _draw_unit->resetActive();
  }
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

} // namespace eg