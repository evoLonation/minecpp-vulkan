import toy;
import math;
import std;
import glm;
import engine.assets;
import engine.base;
// import engine.node;
#include <engine.h>
#include <test.h>
#include <toy.h>

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
  bool                        _type;
  int                         _data;
  std::shared_ptr<TestAsset1> _ref;

  auto assetSerialize() -> AssetPackager override {
    if (_type) {
      return { 0, std::tuple{ _data, AssetRef{ _ref } }, Asset::assetSerialize() };
    } else {
      return { 1, std::tuple{ AssetRef{ _ref }, _data }, Asset::assetSerialize() };
    }
  }
  void assetDeserialize(AssetUnpacker unpacker) override {
    Asset::assetDeserialize(unpacker.extractInherited());
    auto     id = unpacker.getId();
    int      data;
    AssetRef ref;
    if (id == 0) {
      _type = true;
      std::tie(data, ref) = unpacker.extract<int, AssetRef>();
    } else if (id == 1) {
      _type = false;
      std::tie(ref, data) = unpacker.extract<AssetRef, int>();
    }
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
    asset2->_type = true;
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

// TEST(Reval) {
//   {
//     auto  a = Reval<glm::vec3>{ { 1, 2, 3 } };
//     auto& v = a.get();
//     TOY_ASSERT(v == glm::vec3(1, 2, 3));
//     a.set(glm::vec3(4, 5, 6));
//     TOY_ASSERT(v == glm::vec3(4, 5, 6));

//     auto b = Reval<glm::vec3>{ { 7, 8, 9 } };
//     auto f = b.bind(&a, [](glm::vec3 const& value) { return value; });
//     TOY_ASSERT(b.getWithoutUpdate() != v);
//     TOY_ASSERT(b.get() == v);

//     b.unbind(std::move(f));
//     a.set(glm::vec3(10, 11, 12));
//     TOY_ASSERT(b.get() != v);
//   }
//   {
//     auto  b = Reval<glm::vec3>{ { 7, 8, 9 } };
//     auto  a1 = Reval<glm::vec3>{ { 1, 2, 3 } };
//     auto& v1 = a1.getWithoutUpdate();
//     auto  a2 = Reval<glm::vec3>{ { 4, 5, 6 } };
//     auto& v2 = a2.getWithoutUpdate();
//     b.bind(&a1, [](glm::vec3 const& value) { return value; });
//     b.bind(&a2, [](glm::vec3 const& value) { return value; });

//     TOY_ASSERT(v1 != v2);
//     TOY_ASSERT(b.get() == v2);

//     a1.set(glm::vec3(7, 8, 9));
//     TOY_ASSERT(b.get() == v1);
//   }

//   {
//     /**
//      * a > b > a
//      */
//     auto  a = Reval<int>{ 1 };
//     auto& va = a.getWithoutUpdate();
//     auto  b = Reval<int>{ 10 };
//     auto& vb = b.getWithoutUpdate();

//     b.bind(&a, [](auto& value) { return value + 1; });
//     TOY_ASSERT(vb != va + 1);
//     a.bind(&b, [](auto& value) { return value + 2; });
//     TOY_ASSERT(a.get() == 12);
//     TOY_ASSERT(b.get() == 13);
//     TOY_ASSERT(a.get() == 15);

//     // can not double bind
//     try {
//       b.bind(&a, [](auto& value) { return value + 1; });
//       throw std::string("error");
//     } catch (std::exception& e) {
//       toy::debugf({}, "(if print this, is normal) catched error: {}", e.what());
//     }
//   }

//   {
//     // multi bind
//     auto  a = Reval<int>{ 1 };
//     auto& va = a.getWithoutUpdate();
//     auto  b1 = Reval<int>{ 10 };
//     auto  b2 = Reval<int>{ 20 };

//     a.bind([](int v1, int v2) { return v1 + v2; }, &b1, &b2);
//     TOY_ASSERT(a.get() == 30);
//     b1.bind([](int v) { return v + 1; }, &a);
//     TOY_ASSERT(b1.get() == 31);
//     TOY_ASSERT(a.get() == 51);
//     b2.set(30);
//     TOY_ASSERT(a.get() == 82);
//   }

//   {
//     // destruct
//     auto a = Reval<int>{ 1 };
//     auto c = Reval<int>{ 2 };
//     {
//       auto b = Reval<int>{ 10 };
//       b.bind(&a, [](auto& value) { return value + 1; });
//       c.bind(&b, [](auto& value) { return value + 1; });
//       TOY_ASSERT(c.get() == 3);
//     }
//     TOY_ASSERT(c.get() == 3);
//     TOY_ASSERT(a.get() == 1);
//   }

//   {
//     // bindPair
//     auto a = Reval<int>{ 1 };
//     auto b = Reval<int>{ 10 };

//     auto u = a.bindPair(&b);
//     TOY_ASSERT(a.get() == 10);
//     a.set(2);
//     TOY_ASSERT(b.get() == 2);
//     a.unbind(std::move(u));
//   }

//   {
//     auto location = Reval<glm::vec3>{ { 1, 2, 3 } };
//     auto rotation = Reval<glm::vec3>{ { 1, 2, 3 } };

//     auto middle = Reval<std::tuple<glm::vec3, glm::vec3>>{ { glm::vec3{ 4, 5, 6 }, glm::vec3{} }
//     };
//     // middle.bind(
//     //   [](glm::vec3 const& location, glm::vec3 const& rotation) {
//     //     return std::tuple{ location, rotation };
//     //   },
//     //   &location,
//     //   &rotation
//     // );
//     location.bind([](auto const& tuple) { return std::get<0>(tuple); }, &middle);
//     rotation.bind([](auto const& tuple) { return std::get<1>(tuple); }, &middle);
//     location.setName("location");
//     location.set(glm::vec3{ 1, 2, 3 });
//     rotation = glm::vec3{ 1, 2, 3 };
//     auto model = Reval<glm::mat4>{};
//     model.setName("model");
//     model.setTrack(true);
//     auto computer = [](glm::vec3 const& location, glm::vec3 const& rotation) {
//       return mt::translate(location) * mt::rotate(rotation);
//     };
//     model.bind(computer, &location, &rotation);

//     middle.bind(
//       [](glm::mat4 model) {
//         auto location = glm::vec3{ model[3] };
//         model[3] = glm::vec4{ 0, 0, 0, 1 };
//         auto rotate_euler = mt::getEulerAngle(model);
//         return std::tuple{ location, rotate_euler };
//       },
//       &model
//     );
//     TOY_ASSERT(mt::eq(model.get(), computer(glm::vec3(1, 2, 3), glm::vec3(1, 2, 3))));
//     // location = glm::vec3{ 4, 5, 6 };
//     // TOY_ASSERT(mt::eq(model.get(), computer(location.get(), rotation.get())));
//     // TOY_ASSERT(location == glm::vec3(4, 5, 6));
//   }
// }

// TEST(ModelController) {
//   {
//     auto controller = ModelController{};
//     auto model = Reval<glm::mat4>{};
//     controller.setModelTransform(&model, ModelController::CHANGE_MODEL);
//     // TOY_DEBUG(model.get());
//     // controller.refLocation() = glm::vec3{ 1, 2, 3 };
//     // TOY_DEBUG(model.get());
//     // model.set(mt::translate(glm::vec3{ 4, 5, 6 }));
//     // TOY_DEBUG(controller.refLocation().get());
//     // controller.refScaleFactor() = glm::vec3{ 1, 2, 3 };
//     // controller.refScaleFactor().set([](auto& value) { value *= 2; });
//     // TOY_DEBUG(model.get());

//     controller.refLocation().set(glm::vec3{ 7, 8, 9 });
//     TOY_DEBUG(model.get());
//     TOY_DEBUG(controller.refRotateEuler().get());

//     auto controller2 = ModelController{};
//     auto model2 = Reval<glm::mat4>{};
//     controller2.setModelTransform(&model2, ModelController::CHANGE_MODEL);
//     controller2.refLocation().bindPair(&controller.refLocation());

//     controller2.translate(glm::vec3{ 1, 2, 3 });
//     TOY_DEBUG(model.get());
//   }

//   {
//     auto controller = ModelController{};
//     controller.setName("controller");
//     auto model = Reval<glm::mat4>{};
//     model.setName("model");
//     controller.setModelTransform(&model, ModelController::CHANGE_MODEL);

//     auto controller2 = ModelController{};
//     controller2.setName("controller2");
//     controller2.setModelTransform(&model, ModelController::CHANGE_CONTROLLER);

//     controller2.refLocation().setTrack(true);
//     controller2.refLocation().set(glm::vec3{ 1, 2, 3 });

//     controller.refLocation().setTrack(true);
//     TOY_DEBUG(controller.refLocation().get());
//   }
// }

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
}