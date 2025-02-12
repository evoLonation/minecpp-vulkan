// #include <test.h>
import toy;
import math;
import std;
import glm;
#include <enum.h>
#include <test.h>
#include <toy.h>

using namespace toy;

TEST(log) {
  constexpr auto location = std::source_location::current();
  constexpr auto func = location.function_name();
  // debugf(NoLocation{}, "{}", "no location");
  debugs("{}", "with location");
  try {
    throwf({}, false, "without location");
  } catch (const std::runtime_error& err) {
    debug({}, err.what());
  }
  try {
    throwf("asdfads{}", "");
  } catch (const std::runtime_error& err) {
    debug(err.what());
  }
  debugf({}, "{}", "hello");
  debugf(std::source_location::current(), "{}", "hello");
  debugf("{}", "hello");

  checkf({}, "asd");
  // ambiguous
  // checkf({}, "{}", "hello");
  checkf(std::nullopt, "{}", "hello");
}

TEST(macro) {
  int a = 1;
  int b = 2;
  using namespace toy;
  TOY_CHECK(a == b);
  try {
    TOY_ASSERT(a == b, a);
  } catch (const std::runtime_error& err) {
    using namespace std::literals;
    TOY_CHECK("assert error: a == b"s.find(err.what()) != std::string::npos, err.what());
  }
  TOY_DEBUG(a, b);
}

struct CustomComponent : toy::Traceable {
  CustomComponent() { std::cout << "CustomComponent()" << std::endl; }
  ONLY_MOVEABLE(CustomComponent);
  ~CustomComponent() { std::cout << "~CustomComponent()" << std::endl; }
};
struct Custom2 : CustomComponent {};

TEST(traceable) {
  // default construct
  auto cp = Custom2{};
  auto proxy = cp.getTracer();
  TOY_ASSERT(proxy.valid() && &proxy.get() == &cp);
  // move construct
  auto cp2 = std::move(cp);
  TOY_ASSERT(proxy.valid() && &proxy.get() == &cp2);
  // move assign
  auto cp3 = Custom2{};
  auto proxy3 = cp3.getTracer();
  cp3 = std::move(cp2);
  TOY_ASSERT(proxy.valid() && &proxy.get() == &cp3);
  TOY_ASSERT(!proxy3.valid());
  {
    // destruct
    auto cp4 = std::move(cp3);
  }
  TOY_ASSERT(!proxy.valid());

  // tracer construct
  cp = Custom2{};
  proxy = cp.getTracer();
  auto copy = proxy;
  TOY_ASSERT(&copy.get() == &proxy.get());
  auto move = std::move(proxy);
  TOY_ASSERT(!proxy.valid());
  TOY_ASSERT(&move.get() == &copy.get());
  copy = {};
  TOY_ASSERT(!copy.valid());
  copy = move;
  TOY_ASSERT(&copy.get() == &move.get());
  proxy = std::move(move);
  TOY_ASSERT(!move.valid());
  TOY_ASSERT(&proxy.get() == &copy.get());
  TOY_ASSERT(&cp == &proxy.get());
  cp2 = std::move(cp);
  TOY_ASSERT(&cp2 == &proxy.get() && &cp2 == &copy.get());
  {
    auto cp3 = std::move(cp2);
  }
  TOY_ASSERT(!proxy.valid() && !copy.valid());
}

template <typename T>
void testSerializer(T t, std::source_location location = std::source_location::current()) {
  auto buf = std::vector<std::byte>{};
  auto stream = VectorStream{ &buf };
  stream.write(t);
  auto t2 = stream.read<T>();
  toy::throwf(location, t == t2, "t !== t2");
}

struct TrivialTest {
  int         a;
  char        c[13];
  double      b;
  friend auto operator==(TrivialTest const& lhs, TrivialTest const& rhs) -> bool {
    return lhs.a == rhs.a && lhs.b == rhs.b && std::memcmp(lhs.c, rhs.c, sizeof(lhs.c)) == 0;
  }
};

TEST(TrivialSerializer) { testSerializer(TrivialTest{ 1, { 'c' }, 2.0 }); }

TEST(StlSerializer2) {
  testSerializer(std::vector<int>{ 1, 2, 3 });
  testSerializer(std::list<int>{ 4, 5, 6, 7 });
  testSerializer(std::deque<double>{ 8, 9, 10, 11, 12 });
  testSerializer(std::map<int, std::string>{ { 1, "1" }, { 2, "2" }, { 3, "3" } });
  testSerializer(std::unordered_map<int, std::string>{ { 4, "4" }, { 5, "5" }, { 6, "6" } });
  testSerializer(std::unordered_set<int>{ 1, 2, 3, 4 });
  testSerializer(std::set<int>{ 1, 2, 3, 4 });
  testSerializer(std::pair<int, std::string>{ 1, "1" });
  testSerializer(std::tuple{ 8, std::string{ "giao" }, 9.0 });
}

struct MoveOnly {
  int aa;
  MoveOnly() = default;
  MoveOnly(int a) : aa(a) {}
  MoveOnly(const MoveOnly&) noexcept = delete;
  MoveOnly(MoveOnly&&) noexcept = default;
  auto operator=(const MoveOnly&) noexcept -> MoveOnly& = delete;
  auto operator=(MoveOnly&&) noexcept -> MoveOnly& = default;
};

struct Test {
  int                a;
  std::string        b;
  std::vector<float> c;
  MoveOnly           d;

  friend auto operator==(Test const& lhs, Test const& rhs) -> bool {
    TOY_ASSERT(lhs.a == rhs.a, lhs.a, rhs.a);
    TOY_ASSERT(lhs.d.aa == rhs.d.aa, lhs.d.aa, rhs.d.aa);
    return lhs.a == rhs.a && lhs.b == rhs.b && lhs.c == rhs.c && lhs.d.aa == rhs.d.aa;
  }
};

struct TestSerializer : CustomSerializer<
                          Test,
                          [](int a, std::string b, std::vector<float> c, MoveOnly d) {
                            TOY_DEBUG(d.aa);
                            return Test{ a, b, std::move(c), std::move(d) };
                          },
                          MemberSerializerInfo{ &Test::a },
                          MemberSerializerInfo{ &Test::b },
                          MemberSerializerInfo{ &Test::c },
                          MemberSerializerInfo{ &Test::d }> {};

TEST(CustomSerializer) {
  auto buf = std::vector<std::byte>{};
  auto stream = VectorStream{ &buf };
  auto t = Test{ 1, "2", { 3.0, 4.0, 5.0 }, 6 };
  stream.write(t, TestSerializer{});
  auto t2 = stream.read<Test>(TestSerializer{});
  TOY_ASSERT(t == t2);
}

TEST(json) {
  using namespace json;
  auto j0 = Json{};
  TOY_ASSERT(j0.is<Object>());
  TOY_ASSERT(j0.type() == Type::OBJECT);
  auto j1 = Json{ null };
  TOY_ASSERT(j1.is<Null>());
  TOY_ASSERT(j1.type() == Type::NULL);
  auto j2 = Json{ 1 };
  auto j4 = Json{ true };
  TOY_ASSERT(j4.is<Bool>());
  TOY_ASSERT(j4.type() == Type::BOOL);
  auto j5 = Json{ 1.0f };
  TOY_ASSERT(j5.is<Number>());
  TOY_ASSERT(j5.type() == Type::NUMBER);
  auto j6 = Json{ 1.0 };
  TOY_ASSERT(j6.is<Number>());
  TOY_ASSERT(j6.type() == Type::NUMBER);
  auto j7 = Json{ std::string{ "123" } };
  TOY_ASSERT(j7.is<String>());
  TOY_ASSERT(j7.type() == Type::STRING);
  auto j8 = Json{ "123" };
  TOY_ASSERT(j8.is<String>());
  TOY_ASSERT(j8.type() == Type::STRING);
  auto j9 = Json{ { std::string("Xiaoming"), std::string("Genshin") } };
  TOY_ASSERT(j9.is<Object>());
  TOY_ASSERT(j9.type() == Type::OBJECT);
  auto j10 = Json::array({ { { "Xiaoming", j9 } }, std::string("Genshin") });
  TOY_ASSERT(j10.is<List>());
  TOY_ASSERT(j10.type() == Type::LIST);
  j0["123"] = 456;
  TOY_ASSERT(j0["123"].to<Number>() == 456);
  TOY_ASSERT(j0["123"] == 456);
  auto  json = Json::parse(std::ifstream("test.json", std::ios_base::in));
  auto& object = json.to<Object>();
  TOY_ASSERT(object.at("a").toInteger() == 1, "error test json");
  TOY_ASSERT(object.at("b").to<List>()[0].toInteger() == 1, "error test json");
  toy::debug(json.to<Object>() | views::keys);
  toy::debug(json.dump());
  toy::debug(json);
}

Generator foo() {
  int i = 0;
  while (co_yield i) {
    toy::debugf("ready yield {}", i++);
  }
}

TEST(Generator) {
  toy::debug("call foo()");
  auto generator = foo();
  toy::debug(generator.next());
  toy::debug(generator.next());
  toy::debug(generator.next());
  toy::debug(generator.next());
  toy::debug(generator.next());
  toy::debug("call foo() done");
}

TOY_ENUM(Giao, YUANSHEN, QIDONG, YIGEIWOLI);

TEST(Enum) {
  static_assert(Giao::count == 3);

  Giao       a = Giao::YIGEIWOLI;
  Giao::Enum b = a;
  int        c = a;
  a = b;
  a = c;
  a = Giao::YUANSHEN;
  toy::throwf(std::string("YUANSHEN") == a.str(), "error test enum");
  auto format = std::format("{}", a);
}

enum class Giao2 { A, B, C, MAX_ENUM_VALUE };

TEST(EnumSet) {
  static_assert(ranges::input_range<EnumSet<Giao2>>);
  auto set = EnumSet<Giao2>{};
  for (Giao2 a : set) {
    toy::throwf(a != Giao2::B && a != Giao2::MAX_ENUM_VALUE, "enumset test wrong");
  }
  set = EnumSet<Giao2>{ Giao2::A, Giao2::C };
}
