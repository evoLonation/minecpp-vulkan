import toy;
import toy.persistent;
import math;
import std;
import glm;
import engine.assets;
#include <test.h>
#include <toy.h>

using namespace eg;

struct TestAsset1 : public Asset {
public:
  TestAsset1() = default;

  std::vector<glm::vec3> positions;
  std::vector<uint16>    indices;

  auto assetSerialize() const { return std::tuple{ positions, indices }; }

  static auto assetDeserialize(std::vector<glm::vec3> positions, std::vector<uint16> indices)
    -> std::shared_ptr<TestAsset1> {
    auto ret = std::make_shared<TestAsset1>();
    ret->positions = std::move(positions);
    ret->indices = std::move(indices);
    return std::move(ret);
  }

  static inline fs::path _asset_path = "test1";
};

struct TestAsset2 : public Asset {
public:
  int                         _data;
  std::shared_ptr<TestAsset1> _ref;

  auto assetSerialize() const { return std::tuple{ _data, AssetRef{ _ref } }; }

  static auto assetDeserialize(int data, AssetRef<TestAsset1> ref) -> std::shared_ptr<TestAsset2> {
    auto ret = std::make_shared<TestAsset2>();
    ret->_data = data;
    ret->_ref = ref.consume();
    return std::move(ret);
  }

  static inline fs::path _asset_path = "test2";
};

TEST(AssetManager1) {
  auto manager = AssetManager{};
  manager.clearAssets<TestAsset1>();

  auto asset1 = std::make_shared<TestAsset1>();
  asset1->positions = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
  asset1->indices = { 0, 1, 2, 1, 2, 3, 2, 3, 4 };
  asset1->setAssetName("my_asset1");
  manager.save(asset1);
  manager.load<TestAsset1>("my_asset1");
  auto asset1_1 = manager.load<TestAsset1>("my_asset1");
  TOY_ASSERT(
    asset1_1->positions == asset1->positions && asset1_1->indices == asset1->indices &&
    asset1_1->getAssetName() == "my_asset1"
  );
}

TEST(AssetManager2) {
  auto manager = AssetManager{};
  manager.clearAssets<TestAsset1>();
  manager.clearAssets<TestAsset2>();
  {
    auto asset1 = std::make_shared<TestAsset1>();
    asset1->positions = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
    asset1->indices = { 0, 1, 2, 1, 2, 3, 2, 3, 4 };

    auto asset2 = std::make_shared<TestAsset2>();
    asset2->_data = 123;
    asset2->_ref = asset1;
    asset2->setAssetName("my_asset2");
    manager.save(asset2);
    asset2 = manager.load<TestAsset2>("my_asset2");
  }
  {
    auto origin_2 = TestAsset2{};
    origin_2._data = 123;
    auto origin_1 = TestAsset1{};
    origin_1.positions = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
    origin_1.indices = { 0, 1, 2, 1, 2, 3, 2, 3, 4 };
    auto asset2 = manager.load<TestAsset2>("my_asset2");
    TOY_ASSERT(
      asset2->_data == origin_2._data && asset2->_ref->positions == origin_1.positions &&
      asset2->_ref->indices == origin_1.indices
    );
    auto names = manager.getAssetNames<TestAsset2>();
    TOY_ASSERT(names.size() == 1 && names[0] == "my_asset2");
  }
}
