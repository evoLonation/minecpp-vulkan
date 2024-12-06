import toy;
import toy.persistent;
import math;
import std;
import glm;
import engine.assets;
#include <test.h>
#include <toy.h>
#include <engine.h>

using namespace eg;

struct TestAsset1 : public Asset {
public:
  TestAsset1() = default;

  std::vector<glm::vec3> positions;
  std::vector<uint16>    indices;

  auto assetSerialize() -> AssetPackager override {
    return { std::tuple{ positions, indices }, Asset::assetSerialize() };
  }
  void assetDeserialize(AssetUnpacker unpacker) override {
    Asset::assetDeserialize(unpacker.extractInherited());
    std::tie(positions, indices) = unpacker.extract<std::vector<glm::vec3>, std::vector<uint16>>();
  }
  REGISTER_ASSET_PATH("test1")
};

struct TestAsset2 : public Asset {
public:
  int                         _data;
  std::shared_ptr<TestAsset1> _ref;

  auto assetSerialize() -> AssetPackager override {
    return { std::tuple{ _data, AssetRef{ _ref } }, Asset::assetSerialize() };
  }
  void assetDeserialize(AssetUnpacker unpacker) override {
    Asset::assetDeserialize(unpacker.extractInherited());
    auto [data, ref] = unpacker.extract<int, AssetRef>();
    _data = data;
    _ref = ref.consume<TestAsset1>();
  }
  REGISTER_ASSET_PATH("test2")
};

auto getManager() -> AssetManager {
  auto manager = AssetManager{ {
    { TestAsset1::asset_path, []() { return std::make_shared<TestAsset1>(); } },
    { TestAsset2::asset_path, []() { return std::make_shared<TestAsset2>(); } },
  } };
  manager.clearAssets<TestAsset1>();
  manager.clearAssets<TestAsset2>();
  return manager;
}

TEST(AssetManager1) {
  auto manager = getManager();

  auto guid = Guid{};
  auto asset1 = std::make_shared<TestAsset1>();
  asset1->positions = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
  asset1->indices = { 0, 1, 2, 1, 2, 3, 2, 3, 4 };
  asset1->setAssetName("my_asset1");
  manager.save(asset1);
  guid = asset1->getAssetGuid();
  manager.load<TestAsset1>("my_asset1");
  auto asset1_1 = manager.load<TestAsset1>("my_asset1");
  TOY_ASSERT(
    asset1_1->positions == asset1->positions && asset1_1->indices == asset1->indices &&
    asset1_1->getAssetName() == "my_asset1" && asset1_1->getAssetGuid() == guid
  );
}

TEST(AssetManager2) {
  auto manager = getManager();

  auto guid2 = Guid{};
  {
    auto asset1 = std::make_shared<TestAsset1>();
    asset1->positions = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
    asset1->indices = { 0, 1, 2, 1, 2, 3, 2, 3, 4 };

    auto asset2 = std::make_shared<TestAsset2>();
    asset2->_data = 123;
    asset2->_ref = asset1;
    asset2->setAssetName("my_asset2");
    manager.save(asset2);
    guid2 = asset2->getAssetGuid();
    asset2 = manager.load<TestAsset2>("my_asset2");
    TOY_ASSERT(asset2->getAssetGuid() == guid2);
  }
  {
    auto origin_2 = TestAsset2{};
    origin_2._data = 123;
    auto origin_1 = TestAsset1{};
    origin_1.positions = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
    origin_1.indices = { 0, 1, 2, 1, 2, 3, 2, 3, 4 };
    auto asset2 = manager.load<TestAsset2>("my_asset2");
    TOY_ASSERT(asset2->getAssetGuid() == guid2, asset2->getAssetGuid().get(), guid2.get());
    TOY_ASSERT(
      asset2->_data == origin_2._data && asset2->_ref->positions == origin_1.positions &&
      asset2->_ref->indices == origin_1.indices
    );
    auto names = manager.getAssetNames<TestAsset2>();
    TOY_ASSERT(names.size() == 1 && names[0] == "my_asset2");
  }
  {
    auto asset2 = manager.load(guid2);
    TOY_ASSERT(dynamic_cast<TestAsset2*>(asset2.get()) != nullptr);
  }
}
