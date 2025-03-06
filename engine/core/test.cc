import toy;
import math;
import std;
import glm;
import engine.package;
import engine.base;
// import engine.node;
#include <engine.h>
#include <test.h>
#include <toy.h>

using namespace eg;

TEST(LinkedFile) {
  if (fs::exists("test_linked_file")) {
    TOY_ASSERT(!fs::is_directory("test_linked_file"));
    fs::remove("test_linked_file");
  }
  auto page_1 = LinkedFile::PageNumber{};
  auto page_2 = LinkedFile::PageNumber{};
  {

    auto file = LinkedFile{ "test_linked_file" };

    TOY_ASSERT(file.getAll().size() == 0);
    page_1 = file.allocate();
    // first page must be 1
    TOY_ASSERT(page_1 == 1);
    TOY_ASSERT(file.getAll().size() == 1);

    file.setCurrent(page_1);

    auto recover = file.snapshot();
    file.write(std::string{ "hello world" });
    recover.recover();
    auto data = file.read<std::string>();
    TOY_ASSERT(data == "hello world", data);

    std::array<char, 102400> large_data{};
    // fill random data
    std::generate(large_data.begin(), large_data.end(), []() { return std::rand() % 256; });
    recover = file.snapshot();
    file.write(large_data);
    // judge if data is correct
    recover.recover();
    auto read_data = file.read<std::array<char, 102400>>();
    TOY_ASSERT(read_data == large_data);

    {
      // snapshot test
      auto recover = file.snapshot();
      {
        auto recover = file.snapshot();
        {
          auto recover = file.snapshot();
          file.write(123);
          recover.recover();
        }
        TOY_ASSERT(file.read<int>() == 123);
      }
      file.write(456);
      recover.recover();
      TOY_ASSERT(file.read<int>() == 456);
    }
    page_2 = file.allocate();
    auto pages = file.getAll();
    TOY_ASSERT(
      pages.size() == 2 && std::find(pages.begin(), pages.end(), page_1) != pages.end() &&
      std::find(pages.begin(), pages.end(), page_2) != pages.end()
    );
    file.setCurrent(page_2);
    recover = file.snapshot();
    file.write(789);
    recover.recover();
    TOY_ASSERT(file.read<int>() == 789);
  }

  {
    // open a existed file
    auto file = LinkedFile{ "test_linked_file" };
    file.release(page_1);
    auto pages = file.getAll();
    TOY_ASSERT(pages.size() == 1 && pages[0] == page_2);
    file.release(page_2);
    TOY_ASSERT(file.getAll().size() == 0);
  }

  fs::remove("test_linked_file");
}

struct TestAsset1 : public Asset {
public:
  TestAsset1() = default;

  std::vector<glm::vec3> positions;
  std::vector<uint16>    indices;

  void assetSerialize(AssetPackager& packager) override { packager.pack(positions, indices); }
  void assetDeserialize(AssetUnpacker& unpacker) override {
    std::tie(positions, indices) = unpacker.unpack<std::vector<glm::vec3>, std::vector<uint16>>();
  }

  friend auto operator==(TestAsset1 const& lhs, TestAsset1 const& rhs) -> bool {
    return lhs.positions == rhs.positions && lhs.indices == rhs.indices;
  }

private:
  REGISTER_ASSET(TestAsset1);
};

struct TestAssetMember1 : public Asset {
public:
  int data = 0;

  void assetSerialize(AssetPackager& packager) override { packager.pack(data); }
  void assetDeserialize(AssetUnpacker& unpacker) override {
    std::tie(data) = unpacker.unpack<int>();
  }

  friend auto operator==(TestAssetMember1 const& lhs, TestAssetMember1 const& rhs) -> bool {
    return lhs.data == rhs.data;
  }

private:
  REGISTER_ASSET(TestAssetMember1);
};

struct TestAsset2 : public Asset {
public:
  bool                              _type;
  int                               _data;
  std::shared_ptr<TestAsset1>       _ref;
  std::unique_ptr<TestAssetMember1> _member;

  void assetSerialize(AssetPackager& packager) override {
    if (_type) {
      packager.pack(0);
      packager.pack(_data, _ref, _member);
    } else {
      packager.pack(1);
      packager.pack(_ref, _data, _member);
    }
  }
  void assetDeserialize(AssetUnpacker& unpacker) override {
    auto id = std::get<0>(unpacker.unpack<int>());
    if (id == 0) {
      _type = true;
      unpacker.unpack(_data, _ref, _member);
    } else if (id == 1) {
      _type = false;
      unpacker.unpack(_ref, _data, _member);
    }
  }

  friend auto operator==(TestAsset2 const& lhs, TestAsset2 const& rhs) -> bool {
    return lhs._type == rhs._type && lhs._data == rhs._data && *lhs._ref == *rhs._ref &&
           *lhs._member == *rhs._member;
  }
  static auto weakEqual(TestAsset2 const& lhs, TestAsset2 const& rhs) -> bool {
    return lhs._type == rhs._type && lhs._data == rhs._data;
  }

private:
  REGISTER_ASSET(TestAsset2);
};

auto asset1Constructor() -> std::shared_ptr<TestAsset1> {
  auto asset = std::make_shared<TestAsset1>();
  asset->positions = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
  asset->indices = { 0, 1, 2, 1, 2, 3, 2, 3, 4 };
  return asset;
}

TEST(Package1) {
  if (fs::exists("assets/test")) {
    TOY_ASSERT(!fs::is_directory("assets/test"));
    fs::remove("assets/test");
  }
  auto guid_asset1 = Guid{};
  {
    auto package = Package::get("assets/test");
    {
      {
        auto asset1 = asset1Constructor();
        guid_asset1 = asset1->getAssetGuid();
        asset1->setOwnedPackage(package.get());
        asset1->saveAsset();
      }
      auto asset1 = package->getAssetShared<TestAsset1>(guid_asset1);
      TOY_ASSERT(*asset1 == *asset1Constructor());
      TOY_ASSERT(asset1->getAssetGuid() == guid_asset1);
      TOY_ASSERT(asset1->getOwnedPackage() == package.get());
    }
  }
  auto package = Package::get("assets/test");
  auto asset1 = package->getAssetShared<TestAsset1>(guid_asset1);
  TOY_ASSERT(*asset1 == *asset1Constructor());
  TOY_ASSERT(asset1->getAssetGuid() == guid_asset1);
  TOY_ASSERT(asset1->getOwnedPackage() == package.get());

  // test change owned package of asset
  if (fs::exists("assets/test2")) {
    TOY_ASSERT(!fs::is_directory("assets/test2"));
    fs::remove("assets/test2");
  }
  auto package2 = Package::get("assets/test2");
  {
    asset1->setOwnedPackage(package2.get());
    TOY_ASSERT(asset1->getOwnedPackage() == package2.get());
    try {
      package->getAssetShared<TestAsset1>(guid_asset1);
      TOY_ASSERT(false);
    } catch (std::exception& e) {
      toy::debugf("catched error: {}", e.what());
    }
  }
  asset1.reset();
  auto asset2 = package2->getAssetShared<TestAsset1>(guid_asset1);
  TOY_ASSERT(*asset2 == *asset1Constructor());
}

TEST(AssetName) {
  if (fs::exists("assets/test")) {
    TOY_ASSERT(!fs::is_directory("assets/test"));
    fs::remove("assets/test");
  }
  // setAssetName
  auto package = Package::get("assets/test");
  // setAssetName before setOwnedPackage
  {
    auto asset1 = asset1Constructor();
    asset1->setAssetName("asset1");
    asset1->setOwnedPackage(package.get());
  }
  {
    auto asset1 = package->getAssetShared<TestAsset1>("asset1");
    TOY_ASSERT(asset1->getAssetName() == "asset1");
  }
  // setAssetName after setOwnedPackage
  {
    auto asset1 = asset1Constructor();
    asset1->setOwnedPackage(package.get());
    asset1->setAssetName("asset2");
  }
  {
    auto asset1 = package->getAssetShared<TestAsset1>("asset2");
    TOY_ASSERT(asset1->getAssetName() == "asset2", asset1->getAssetName());
  }
  // resetAssetName
  {
    auto asset_names = package->getAllAssetNames();
    std::sort(asset_names.begin(), asset_names.end());
    auto expected_asset_names = std::vector<std::string>{ "asset1", "asset2" };
    std::sort(expected_asset_names.begin(), expected_asset_names.end());
    TOY_ASSERT(asset_names == expected_asset_names);
    auto asset1 = package->getAssetShared<TestAsset1>("asset1");
    asset1->resetAssetName();
    asset_names = package->getAllAssetNames();
    TOY_ASSERT(asset_names.size() == 1 && asset_names[0] == "asset2");
  }
  // duplicate name error
  {
    try {
      auto asset1 = asset1Constructor();
      asset1->setOwnedPackage(package.get());
      asset1->setAssetName("asset2");
    } catch (std::exception& e) {
      toy::debugf("catched error: {}", e.what());
    }
    try {
      auto asset1 = asset1Constructor();
      asset1->setAssetName("asset2");
      asset1->setOwnedPackage(package.get());
    } catch (std::exception& e) {
      toy::debugf("catched error: {}", e.what());
    }
  }
}

TEST(Package2) {
  if (fs::exists("assets/test")) {
    TOY_ASSERT(!fs::is_directory("assets/test"));
    fs::remove("assets/test");
  }
  auto package = Package::get("assets/test");
  auto assetConstructor = [](bool type) -> std::shared_ptr<TestAsset2> {
    auto asset = std::make_shared<TestAsset2>();
    asset->_data = 123;
    asset->_ref = std::make_shared<TestAsset1>();
    asset->_ref->positions = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
    asset->_ref->indices = { 0, 1, 2, 1, 2, 3, 2, 3, 4 };
    asset->_member = std::make_unique<TestAssetMember1>();
    asset->_member->data = 456;
    asset->_type = type;
    return asset;
  };

  auto test = [&](bool type) {
    auto guid = Guid{};
    auto guid_ref = Guid{};
    auto guid_member = Guid{};
    {
      // ref of asset only set in package but not save (can just load default value)
      {
        auto asset = assetConstructor(type);
        asset->setOwnedPackage(package.get());
        asset->saveAsset(package.get());
        guid = asset->getAssetGuid();
        guid_ref = asset->_ref->getAssetGuid();
        guid_member = asset->_member->getAssetGuid();
      }
      auto asset = package->getAssetUnique<TestAsset2>(guid);
      TOY_ASSERT(TestAsset2::weakEqual(*asset, *assetConstructor(type)));
      TOY_ASSERT(*asset->_ref == TestAsset1{} && *asset->_member == TestAssetMember1{});
      TOY_ASSERT(asset->getAssetGuid() == guid && asset->getOwnedPackage() == package.get());
      TOY_ASSERT(
        asset->_ref->getAssetGuid() == guid_ref && asset->_ref->getOwnedPackage() == package.get()
      );
      TOY_ASSERT(
        asset->_member->getAssetGuid() == guid_member &&
        asset->_member->getOwnedPackage() == package.get()
      );
    }
    {
      {
        auto asset = assetConstructor(type);
        guid = asset->getAssetGuid();
        asset->setOwnedPackage(package.get());
        asset->saveAsset(package.get());
        asset->_ref->saveAsset(package.get());
        asset->_member->saveAsset(package.get());
      }
      auto asset = package->getAssetUnique<TestAsset2>(guid);
      TOY_ASSERT(*asset == *assetConstructor(type));
    }
    // setOwnedPackageShared
    {
      {
        auto asset = assetConstructor(type);
        guid = asset->getAssetGuid();
        Asset::setOwnedPackageShared(asset, package.get());
        asset->saveAsset(package.get());
        asset->_ref->saveAsset(package.get());
        asset->_member->saveAsset(package.get());
        try {
          auto asset2 = package->getAssetUnique<TestAsset2>(guid);
          TOY_ASSERT(false);
        } catch (std::exception& e) {
          toy::debugf("catched error: {}", e.what());
        }
        auto asset2 = package->getAssetShared<TestAsset2>(guid);
        TOY_ASSERT(asset.get() == asset2.get());
      }
      auto asset = package->getAssetShared<TestAsset2>(guid);
      TOY_ASSERT(*asset == *assetConstructor(type));
    }
  };

  test(true);
  test(false);
}

TEST(Package3) {
  // ref of asset is stored in another package
  if (fs::exists("assets/test")) {
    TOY_ASSERT(!fs::is_directory("assets/test"));
    fs::remove("assets/test");
  }
  if (fs::exists("assets/test_ref")) {
    TOY_ASSERT(!fs::is_directory("assets/test_ref"));
    fs::remove("assets/test_ref");
  }
  auto package = Package::get("assets/test");
  auto package_ref = Package::get("assets/test_ref");

  auto assetConstructor = []() -> std::shared_ptr<TestAsset2> {
    auto asset = std::make_shared<TestAsset2>();
    asset->_data = 123;
    asset->_ref = std::make_shared<TestAsset1>();
    asset->_ref->positions = { { 1, 2, 3 }, { 4, 5, 6 }, { 7, 8, 9 } };
    asset->_ref->indices = { 0, 1, 2, 1, 2, 3, 2, 3, 4 };
    asset->_member = std::make_unique<TestAssetMember1>();
    asset->_member->data = 456;
    asset->_type = true;
    return asset;
  };
  auto asset = assetConstructor();
  asset->setOwnedPackage(package.get());
  asset->saveAsset(package.get());
  // wrong: _ref change its owned package but asset dont know
  asset->_ref->setOwnedPackage(package_ref.get());
  asset->_ref->saveAsset(package_ref.get());
}

TEST(Reval) {
  bool track = true;
  {
    // basic
    auto a = Reval<int>{ 1 };
    auto b = Reval<int>{ 2 };
    a.setName("a");
    b.setName("b");
    a.setTrack(track);
    b.setTrack(track);
    a.bind([](int v) { return v + 1; }, &b);
    b.bind([](int v) { return v + 1; }, &a);
    b.set(3);
    TOY_ASSERT(a.get() == 4);
    a.set(4);
    TOY_ASSERT(b.get() == 5);
  }
  {
    // multi bind
    auto a1 = Reval<float>{ 1 };
    auto a2 = Reval<float>{ 2 };
    auto b1 = Reval<int>{ 3 };
    auto b2 = Reval<int>{ 4 };
    a1.setName("a1");
    a2.setName("a2");
    b1.setName("b1");
    b2.setName("b2");
    a1.setTrack(track);
    a2.setTrack(track);
    b1.setTrack(track);
    b2.setTrack(track);

    MultiBinder{ &a1, &a2 }.bind(
      [](int v1, int v2) { return std::tuple{ v1 + v2, v1 - v2 }; }, &b1, &b2
    );
    MultiBinder{ &b1, &b2 }.bind(
      [](int v1, int v2) { return std::tuple{ v1 + v2, v1 - v2 }; }, &a1, &a2
    );

    b1.set(5);
    TOY_ASSERT(a1.get() == 9);
    TOY_ASSERT(a2.get() == 1);
    b2.set(6);
    TOY_ASSERT(a1.get() == 11);
    TOY_ASSERT(a2.get() == -1);
    a1.set(10);
    TOY_ASSERT(b1.get() == 9);
    TOY_ASSERT(b2.get() == 11);
  }

  {
    // a <-> b1
    // [b1, b2] <-> c
    // [b1, b2] <-> d
    auto common = [&](int chooce) {
      auto a = Reval<int>{ 1 };
      auto b1 = Reval<int>{ 2 };
      auto b2 = Reval<int>{ 3 };
      auto c = Reval<int>{ 4 };
      auto d = Reval<int>{ 5 };
      a.setName("a");
      b1.setName("b1");
      b2.setName("b2");
      c.setName("c");
      d.setName("d");
      a.setTrack(track);
      b1.setTrack(track);
      b2.setTrack(track);
      c.setTrack(track);
      d.setTrack(track);
      auto unbinder1 = a.bind([](int v) { return v + 1; }, &b1);
      b1.bind([](int v) { return v + 1; }, &a);
      MultiBinder{ &b1, &b2 }.bind([](int v) { return std::tuple{ v + 10, v - 10 }; }, &c);
      auto unbinder2 = MultiBinder{ &c }.bind([](int v1, int v2) { return v1 + v2; }, &b1, &b2);
      MultiBinder{ &b1, &b2 }.bind([](int v) { return std::tuple{ v + 20, v - 20 }; }, &d);
      auto unbinder3 = MultiBinder{ &d }.bind([](int v1, int v2) { return v1 - v2; }, &b1, &b2);

      toy::debugf("chooce {}", chooce);
      if (chooce == 0) {
        b1.set(10);
        b2.set(20);
        TOY_ASSERT(c.get() == 30);
        TOY_ASSERT(a.get() == 11);
        TOY_ASSERT(d.get() == -10);

        c.set(40);
        toy::debug("check if immediately update b2");
        a.set(50);
        // b2 update to 30
        // b1 update to 51
        TOY_ASSERT(c.get() == 81);
        TOY_ASSERT(d.get() == 21);
      } else if (chooce == 1) {
        b1.set(60);
        b2.set(70);

        toy::debug("reset b2");
        b2.reset();
        TOY_ASSERT(c.get() == 130);
        TOY_ASSERT(d.get() == -10);
        toy::debug("reset b1");
        b1.reset();
        TOY_ASSERT(a.get() == 61);
      } else if (chooce == 2) {
        b1.set(60);
        b2.set(70);

        toy::debug("unbind b2 and b1");
        unbinder1.unbind();
        unbinder2.unbind();
        unbinder3.unbind();

        TOY_ASSERT(c.get() == 130);
        TOY_ASSERT(d.get() == -10);
        TOY_ASSERT(a.get() == 61);
        // can repeat unbind
        unbinder1.unbind();
      }
    };
    common(0);
    common(1);
    common(2);
  }

  {
    // bind now
    auto a = Reval<int>{ 1 };
    auto b = Reval<int>{ 2 };

    auto u = a.bind([](int x) { return x + 1; }, &b);
    TOY_ASSERT(a.get() == 1);
    b.set(2);
    TOY_ASSERT(a.get() == 3);
    u.unbind();
    b.set(10);
    TOY_ASSERT(a.get() == 3);
    u = a.bindNow([](int x) { return x + 1; }, &b);
    TOY_ASSERT(a.get() == 11);
  }

  {
    // bind pair
    auto a = Reval<int>{ 1 };
    auto b = Reval<int>{ 10 };
    a.bindPair(&b);
    TOY_ASSERT(a.get() == 10);
    a.set(20);
    TOY_ASSERT(b.get() == 20);
  }

  {
    auto common = [&](int choose) {
      // a -> [b, c]
      // c -> b
      // b -> [b1, b2]
      auto a = Reval<int>{ 1 };
      auto b = Reval<int>{ 2 };
      auto b1 = Reval<int>{ 3 };
      auto b2 = Reval<int>{ 4 };
      auto c = Reval<int>{ 3 };
      a.setName("a");
      b.setName("b");
      b1.setName("b1");
      b2.setName("b2");
      c.setName("c");
      a.setTrack(track);
      b.setTrack(track);
      b1.setTrack(track);
      b2.setTrack(track);
      c.setTrack(track);
      a.bind([](int b, int c) { return b + c; }, &b, &c);
      c.bind([](int b) { return b + 1; }, &b);
      b.bind([](int b1, int b2) { return b1 - b2; }, &b1, &b2);

      if (choose == 0) {
        b1.set(20);
        b2.set(10);
        TOY_ASSERT(a.get() == 21);
      } else if (choose == 1) {
        b.bind([](int c) { return c + 1; }, &c);

        b.set(20);
        TOY_ASSERT(a.get() == 41);
        c.set(30);
        TOY_ASSERT(a.get() == 61);
        b1.set(50);
        b2.set(40);
        TOY_ASSERT(a.get() == 21);
      }
    };
    toy::debug("test if compute is correctly ordered");
    // wrong order: push a, push b, push c
    // correct order: push a, push c, push b
    common(0);
    // add bidirectional bind b <-> c
    common(1);
  }
}