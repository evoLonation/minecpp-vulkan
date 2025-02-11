module;
#include <toy.h>
module engine.asset;

namespace eg {

auto Guid::randomChar() -> unsigned char {
  std::random_device              rd;
  std::mt19937                    gen(rd());
  std::uniform_int_distribution<> dis(0, 255);
  return static_cast<unsigned char>(dis(gen));
}

auto Guid::generateHex(const unsigned int len) -> std::string {
  std::stringstream ss;
  for (auto i = 0; i < len; i++) {
    auto              rc = randomChar();
    std::stringstream hexstream;
    hexstream << std::hex << int(rc);
    auto hex = hexstream.str();
    ss << (hex.length() < 2 ? '0' + hex : hex);
  }
  return ss.str();
}

auto Asset::assetSerialize() -> AssetPackager {
  return AssetPackager{ std::tuple{ getAssetGuid().get(), getAssetName() } };
}

void Asset::assetDeserialize(AssetUnpacker unpacker) {
  // a empty extractInherited to avoid error
  unpacker.extractInherited();
  auto [guid, name] = unpacker.extract<std::string, std::string>();
  _guid = std::move(guid);
  _name = std::move(name);
}

void AssetManager::save(std::shared_ptr<Asset> t) {

  _current_working_manager = this;

  bool is_outest_call;
  {
    auto guid = t->getAssetGuid();
    is_outest_call = _saving_already_saved.empty();
    if (_saving_already_saved.contains(guid)) {
      return;
    }
    _saving_already_saved.insert(guid);
  }

  auto guid = t->getAssetGuid();
  auto name = t->getAssetName();
  // check if name is used before
  if (!name.empty()) {
    // create a file map name to guid
    auto name_map_path = _asset_dir / (name + ".asset");
    if (fs::exists(name_map_path)) {
      // get guid and check if it is the same
      auto guid = Guid{};
      if (auto asset = _named_asset_map.find({ _asset_dir, name });
          asset != _named_asset_map.end()) {
        guid = asset->second;
      } else {
        auto pickle = toy::Pickle{ name_map_path };
        guid = Guid{ pickle.pop<std::string>() };
      }
      if (guid != t->getAssetGuid()) {
        toy::throwf("Asset name {} already exists", name);
      }
    }
  }

  auto pickle = toy::Pickle{};
  auto packer = t->assetSerialize();
  packer.pushToPickle(pickle);
  {
    auto& t_ref = *t;
    auto  type_name = std::string{ typeid(t_ref).name() };
    TOY_ASSERT(_default_asset_map.contains(type_name), type_name);
    pickle.push(type_name);
  }
  auto path = _asset_dir / (guid.get() + ".asset");
  if (auto parent_dir = path.parent_path(); !fs::exists(parent_dir)) {
    fs::create_directories(parent_dir);
  }
  pickle.dump(path);
  _assets[guid] = t;
  if (!name.empty()) {
    auto pickle = toy::Pickle{};
    pickle.push(guid.get());
    pickle.dump(_asset_dir / (name + ".asset"));
    _named_asset_map[{ _asset_dir, name }] = guid;
  }

  if (is_outest_call) {
    _saving_already_saved.clear();
  }
}

void AssetManager::save(std::shared_ptr<Asset> t, std::string name) {
  t->setAssetName(std::move(name));
  save(t);
}

auto AssetManager::load(Guid const& guid) -> std::shared_ptr<Asset> {
  _current_working_manager = this;

  if (auto it = _assets.find(guid); it != _assets.end() && !it->second.expired()) {
    return it->second.lock();
  }
  auto pickle = toy::Pickle{ _asset_dir / (guid.get() + ".asset") };
  auto type_name = pickle.pop<std::string>();
  TOY_ASSERT(_default_asset_map.contains(type_name), type_name);
  auto asset = _default_asset_map[type_name].shared_getter();
  asset->assetDeserialize(AssetUnpacker{ &pickle });
  _assets[guid] = asset;
  auto name = asset->getAssetName();
  if (!name.empty()) {
    _named_asset_map[{ _asset_dir, name }] = guid;
  }
  return asset;
}

auto AssetManager::load(std::string const& name) -> std::shared_ptr<Asset> {
  if (auto it = _named_asset_map.find({ _asset_dir, name }); it != _named_asset_map.end()) {
    return load(it->second);
  }
  auto pickle = toy::Pickle{ _asset_dir / (name + ".asset") };
  auto guid = Guid{ pickle.pop<std::string>() };
  return load(guid);
}
void AssetManager::remove(Asset* t) {
  auto guid = t->getAssetGuid();
  auto name = t->getAssetName();
  auto path = _asset_dir / (guid.get() + ".asset");
  fs::remove(path);
  if (!name.empty()) {
    path = _asset_dir / (name + ".asset");
    fs::remove(path);
  }
}

void AssetRefSerializer::serialize(toy::Pickle& pickle, AssetRef const& t) {
  AssetManager::_current_working_manager->save(t._asset);
  pickle.push(t._asset->getAssetGuid(), GuidSerializer{});
}

auto AssetRefSerializer::deserialize(toy::Pickle& pickle) -> AssetRef {
  auto guid = pickle.pop<Guid, GuidSerializer>();
  return AssetRef{ AssetManager::_current_working_manager->load(guid) };
}

void AssetMemberSerializer::serialize(toy::Pickle& pickle, AssetMember const& t) {
  t._to_serialize->assetSerialize().pushToPickle(pickle);
  auto type_name = std::string{ typeid(*t._to_serialize).name() };
  TOY_ASSERT(AssetManager::_default_asset_map.contains(type_name), type_name);
  pickle.push(type_name);
}

auto AssetMemberSerializer::deserialize(toy::Pickle& pickle) -> AssetMember {
  auto type_name = pickle.pop<std::string>();
  TOY_ASSERT(AssetManager::_default_asset_map.contains(type_name), type_name);
  auto asset = AssetManager::_default_asset_map[type_name].unique_getter();
  asset->assetDeserialize(AssetUnpacker{ &pickle });
  auto member = AssetMember{};
  member._from_deserialize = std::move(asset);
  return member;
}

} // namespace eg