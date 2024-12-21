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

AssetManager::AssetManager(
  std::unordered_map<fs::path, std::function<std::shared_ptr<Asset>()>> default_asset_map
) {
  _default_asset_map = std::move(default_asset_map);
  if (!fs::exists(_root_dir)) {
    fs::create_directories(_root_dir);
  }
  loadAssetPaths();
}

AssetManager::~AssetManager() {
  saveAssetPaths();
  for (auto const& asset_dir : _named_asset_map | views::keys) {
    saveNamedAssets(asset_dir);
  }
}

void AssetManager::save(std::shared_ptr<Asset> t) {
  auto guid = t->getAssetGuid();
  bool is_outest_call = _saving_already_saved.empty();
  if (_saving_already_saved.contains(guid)) {
    return;
  }
  _saving_already_saved.insert(guid);
  auto name = t->getAssetName();
  auto asset_dir = t->getAssetPath();
  _asset_path_map[guid] = asset_dir;
  _assets[asset_dir][guid] = t;
  if (!name.empty()) {
    if (!_named_asset_map.contains(asset_dir)) {
      loadNamedAssets(asset_dir);
    }
    toy::throwf(!_named_asset_map[asset_dir].contains(name), "Asset name {} already exists", name);
    _named_asset_map[asset_dir][name] = guid;
  }
  auto pickle = toy::Pickle{};
  auto packer = t->assetSerialize();
  packer.pushToPickle(pickle);
  auto path = _root_dir / asset_dir / (guid.get() + ".asset");
  if (auto parent_dir = path.parent_path(); !fs::exists(parent_dir)) {
    fs::create_directories(parent_dir);
  }
  pickle.dump(path);
  if (is_outest_call) {
    _saving_already_saved.clear();
  }
}

void AssetManager::save(std::shared_ptr<Asset> t, std::string name) {
  t->setAssetName(std::move(name));
  save(t);
}

auto AssetManager::load(Guid const& guid) -> std::shared_ptr<Asset> {
  auto  asset = getDefaultAssetByPath(_asset_path_map.at(guid));
  auto  asset_dir = asset->getAssetPath();
  auto& assets = _assets[asset_dir];
  if (auto it = assets.find(guid); it != assets.end() && !it->second.expired()) {
    return it->second.lock();
  }
  auto pickle = toy::Pickle{ _root_dir / asset_dir / (guid.get() + ".asset") };
  asset->assetDeserialize(AssetUnpacker{ &pickle });
  assets[guid] = asset;
  return asset;
}

auto AssetManager::load(std::string const& name, fs::path const& asset_dir)
  -> std::shared_ptr<Asset> {
  if (!_named_asset_map.contains(asset_dir)) {
    loadNamedAssets(asset_dir);
  }
  auto guid = _named_asset_map[asset_dir].at(name);
  return load(guid);
}

auto AssetManager::getAssetNames(fs::path const& asset_dir) -> std::vector<std::string> {
  if (!_named_asset_map.contains(asset_dir)) {
    loadNamedAssets(asset_dir);
  }
  return _named_asset_map[asset_dir] | views::keys | ranges::to<std::vector>();
}

void AssetManager::clearAssets(fs::path const& asset_dir) {
  for (auto iter = _asset_path_map.begin(); iter != _asset_path_map.end();) {
    if (iter->second == asset_dir) {
      iter = _asset_path_map.erase(iter);
    } else {
      ++iter;
    }
  }
  for (auto& [guid, asset] : _assets[asset_dir]) {
    if (!asset.expired()) {
      asset.lock()->clear();
    }
  }
  _assets.erase(asset_dir);
  _named_asset_map.erase(asset_dir);
  fs::remove_all(_root_dir / asset_dir);
}

void AssetManager::loadNamedAssets(fs::path const& asset_dir) {
  auto filepath = _root_dir / asset_dir / _named_assets_file;
  if (!fs::exists(filepath)) {
    return;
  }
  auto named_assets =
    json::Json::parse(std::ifstream{ _root_dir / asset_dir / _named_assets_file });
  for (auto const& [name, guid] : named_assets.to<json::Object>()) {
    _named_asset_map[asset_dir][name] = Guid{ guid.to<std::string>() };
  }
}

void AssetManager::saveNamedAssets(fs::path const& asset_dir) {
  auto filepath = _root_dir / asset_dir / _named_assets_file;
  if (auto parent_dir = filepath.parent_path(); !fs::exists(parent_dir)) {
    fs::create_directories(parent_dir);
  }
  auto named_assets = json::Object{};
  for (auto const& [name, guid] : _named_asset_map[asset_dir]) {
    named_assets[name] = guid.get();
  }
  std::ofstream ofs{ filepath };
  ofs << json::Json{ named_assets }.dump();
}

void AssetManager::loadAssetPaths() {
  auto filepath = _root_dir / _asset_path_file;
  if (!fs::exists(filepath)) {
    return;
  }
  auto asset_paths = json::Json::parse(std::ifstream{ filepath });
  for (auto& [path, guids] : asset_paths.to<json::Object>()) {
    for (auto& guid : guids.to<json::List>()) {
      _asset_path_map[Guid{ guid.to<json::String>() }] = fs::path{ path };
    }
  }
}

void AssetManager::saveAssetPaths() {
  auto filepath = _root_dir / _asset_path_file;
  if (auto parent_dir = filepath.parent_path(); !fs::exists(parent_dir)) {
    fs::create_directories(parent_dir);
  }
  auto asset_paths = std::unordered_map<std::string, std::vector<std::string>>{};
  for (auto& [guid, path] : _asset_path_map) {
    asset_paths[path.string()].push_back(guid.get());
  }
  auto json = json::Json{};
  for (auto& [path, guids] : asset_paths) {
    auto guids_json = json::List{};
    for (auto& guid : guids) {
      guids_json.push_back(guid);
    }
    json[path] = guids_json;
  }
  std::ofstream ofs{ filepath };
  ofs << json.dump();
}

auto AssetManager::getDefaultAssetByPath(fs::path const& path) -> std::shared_ptr<Asset> {
  toy::throwf(_default_asset_map.contains(path), "No default asset for path {}", path.string());
  return _default_asset_map[path]();
}

void AssetRefSerializer::serialize(toy::Pickle& pickle, AssetRef const& t) {
  auto& manager = AssetManager::getInstance();
  manager.save(t._asset);
  pickle.push(t._asset->getAssetGuid(), GuidSerializer{});
}

auto AssetRefSerializer::deserialize(toy::Pickle& pickle) -> AssetRef {
  auto& manager = AssetManager::getInstance();
  auto  guid = pickle.pop<Guid, GuidSerializer>();
  return AssetRef{ manager.load(guid) };
}

} // namespace eg