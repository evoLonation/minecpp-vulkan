// module;
#include <toy.h>

// export module toy.persistent;

import toy.trait;
import std;
import toy.log;
#include <test.h>

namespace toy {

template <typename T>
struct DefaultSerializerS;
template <typename T>
using DefaultSerializer = DefaultSerializerS<T>::type;

class Pickle {
public:
  Pickle(std::string_view path) {
    auto file = std::ifstream{};
    file.open(path, std::ios::binary | std::ios::in | std::ios::ate);
    toy::throwf(file.is_open(), "failed to open file {}", path);
    auto file_size = file.tellg();
    _data = std::vector<std::byte>(file_size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(_data.data()), file_size);
    file.close();
    _pos = file_size;
  }
  Pickle() = default;
  void dump(std::string_view path) const {
    auto file = std::ofstream{};
    file.open(path, std::ios::binary | std::ios::out);
    toy::throwf(file.is_open(), "failed to open file {}", path);
    file.write(reinterpret_cast<char const*>(_data.data()), _data.size());
    file.close();
  }

  template <typename T, typename SerializerT = DefaultSerializer<T>>
  void push(T const& t, SerializerT serializer = DefaultSerializer<T>{}) {
    SerializerT::serialize(*this, t);
  }
  void pushBytes(std::span<std::byte const> data) {
    _data.append_range(data);
    _pos += data.size();
  }
  template <typename T, typename SerializerT = DefaultSerializer<T>>
  auto pop() -> T {
    return SerializerT::deserialize(*this);
  }
  void popBytes(std::span<std::byte> data) {
    auto size = data.size();
    toy::throwf(_data.size() >= size, "not enough data");
    std::copy(_data.end() - size, _data.end(), data.begin());
    _data.resize(_data.size() - size);
    _pos -= size;
  }

private:
  std::vector<std::byte> _data;
  std::size_t            _pos = 0;
};

template <typename T>
concept TriviallySerializable =
  std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T> && !std::is_pointer_v<T> &&
  !std::is_reference_v<T> && !std::is_const_v<T>;

template <TriviallySerializable T>
struct TrivialSerializer {
  static void serialize(Pickle& pickle, T const& t) {
    pickle.pushBytes({ reinterpret_cast<std::byte const*>(&t), sizeof(T) });
  }
  static auto deserialize(Pickle& pickle) -> T {
    T t;
    pickle.popBytes({ reinterpret_cast<std::byte*>(&t), sizeof(T) });
    return t;
  }
};
template <TriviallySerializable T>
struct DefaultSerializerS<T> {
  using type = TrivialSerializer<T>;
};

TEST(TrivialSerializer) {
  struct Test {
    int    a;
    char   c[13];
    double b;
  };
  auto pickle = Pickle{};
  auto test = Test{ 1, { 'c' }, 2.0 };
  pickle.push(test);
  pickle.dump("test.pkl");

  pickle = Pickle{ "test.pkl" };
  auto test2 = pickle.pop<Test>();
  TOY_ASSERT(test.a == test2.a && test.b == test2.b && test.c[0] == test2.c[0]);
}

template <typename T, typename ES = DefaultSerializer<typename T::value_type>>
struct SequenceSerializer {
  static void serialize(Pickle& pickle, T const& t) {
    for (auto const& e : t) {
      pickle.push(e, ES{});
    }
    pickle.push(static_cast<std::size_t>(t.size()));
  }
  static auto deserialize(Pickle& pickle) -> T {
    auto size = pickle.pop<std::size_t>();
    auto t = T{};
    t.resize(size);
    for (auto& e : t | views::reverse) {
      e = pickle.pop<typename T::value_type, ES>();
    }
    return t;
  }
};
template <typename T>
struct DefaultSerializerS<std::vector<T>> {
  using type = SequenceSerializer<std::vector<T>>;
};
template <typename T>
struct DefaultSerializerS<std::list<T>> {
  using type = SequenceSerializer<std::list<T>>;
};
template <typename T>
struct DefaultSerializerS<std::deque<T>> {
  using type = SequenceSerializer<std::deque<T>>;
};
template <>
struct DefaultSerializerS<std::string> {
  using type = SequenceSerializer<std::string>;
};

template <
  typename T,
  typename KS = DefaultSerializer<typename T::key_type>,
  typename VS = DefaultSerializer<typename T::mapped_type>>
struct MapSerializer {
  static void serialize(Pickle& pickle, T const& t) {
    for (auto const& [k, v] : t) {
      pickle.push(k, KS{});
      pickle.push(v, VS{});
    }
    pickle.push(static_cast<std::size_t>(t.size()));
  }
  static auto deserialize(Pickle& pickle) -> T {
    auto size = pickle.pop<std::size_t>();
    auto t = T{};
    for (auto i = 0; i < size; ++i) {
      auto v = pickle.pop<typename T::mapped_type, VS>();
      auto k = pickle.pop<typename T::key_type, KS>();
      t.insert({ std::move(k), std::move(v) });
    }
    return t;
  }
};

template <typename K, typename V>
struct DefaultSerializerS<std::map<K, V>> {
  using type = MapSerializer<std::map<K, V>>;
};
template <typename K, typename V>
struct DefaultSerializerS<std::unordered_map<K, V>> {
  using type = MapSerializer<std::unordered_map<K, V>>;
};

template <
  typename T,
  typename FS = DefaultSerializer<typename T::first_type>,
  typename SS = DefaultSerializer<typename T::second_type>>
struct PairSerializer {
  static void serialize(Pickle& pickle, T const& t) {
    pickle.push(t.first, FS{});
    pickle.push(t.second, SS{});
  }
  static auto deserialize(Pickle& pickle) -> T {
    auto second = pickle.pop<typename T::second_type, SS>();
    auto first = pickle.pop<typename T::first_type, FS>();
    return { std::move(first), std::move(second) };
  }
};

template <typename T1, typename T2>
struct DefaultSerializerS<std::pair<T1, T2>> {
  using type = PairSerializer<std::pair<T1, T2>>;
};

// todo: custom element serializer
template <typename T>
struct TupleSeiralizer {
  using SerializerPack = decltype(applyIndexSequence<std::tuple_size_v<T>>([]<size_t... is> {
    return TypePack<DefaultSerializer<std::tuple_element_t<is, T>>...>{};
  }));

  static void serialize(Pickle& pickle, T const& t) {
    // reverse order
    templateForEach<std::tuple_size_v<T>>(
      [&]<size_t index, size_t i = std::tuple_size_v<T> - 1 - index>() {
        pickle.push(std::get<i>(t), typename SerializerPack::template at<i>{});
      }
    );
  }
  static auto deserialize(Pickle& pickle) -> T {
    T t;
    templateForEach<std::tuple_size_v<T>>([&]<size_t i>() {
      std::get<i>(t) =
        pickle.pop<std::tuple_element_t<i, T>, typename SerializerPack::template at<i>>();
    });
    return t;
  }
};

template <typename... Ts>
struct DefaultSerializerS<std::tuple<Ts...>> {
  using type = TupleSeiralizer<std::tuple<Ts...>>;
};

TEST(StlSerializer) {
  auto pickle = Pickle{};
  auto test = std::vector<int>{ 1, 2, 3 };
  pickle.push(test);
  pickle.dump("test.pkl");

  pickle = Pickle{ "test.pkl" };
  auto test2 = pickle.pop<std::vector<int>>();
  TOY_ASSERT(test == test2);

  auto test3 = std::list<int>{ 4, 5, 6, 7 };
  pickle.push(test3);
  pickle.dump("test.pkl");
  pickle = Pickle{ "test.pkl" };
  auto test4 = pickle.pop<std::list<int>>();
  TOY_ASSERT(test3 == test4);

  auto test5 = std::deque<double>{ 8, 9, 10, 11, 12 };
  pickle.push(test5);
  pickle.dump("test.pkl");
  pickle = Pickle{ "test.pkl" };
  auto test6 = pickle.pop<std::deque<double>>();
  TOY_ASSERT(test5 == test6);

  auto test7 = std::map<int, std::string>{ { 1, "1" }, { 2, "2" }, { 3, "3" } };
  pickle.push(test7);
  pickle.dump("test.pkl");
  pickle = Pickle{ "test.pkl" };
  auto test8 = pickle.pop<std::map<int, std::string>>();
  TOY_ASSERT(test7 == test8);
  auto test9 = std::unordered_map<int, std::string>{ { 4, "4" }, { 5, "5" }, { 6, "6" } };
  pickle.push(test9);
  pickle.dump("test.pkl");
  pickle = Pickle{ "test.pkl" };
  auto test10 = pickle.pop<std::unordered_map<int, std::string>>();
  TOY_ASSERT(test9 == test10);

  auto test11 = std::pair<int, std::string>{ 7, "7" };
  pickle.push(test11);
  pickle.dump("test.pkl");
  pickle = Pickle{ "test.pkl" };
  auto test12 = pickle.pop<std::pair<int, std::string>>();
  TOY_ASSERT(test11 == test12);

  auto test13 = std::tuple{ 8, std::string{ "8" }, 9.0 };
  pickle.push(test13);
  pickle.dump("test.pkl");
  pickle = Pickle{ "test.pkl" };
  auto test14 = pickle.pop<std::tuple<int, std::string, double>>();
  TOY_ASSERT(test13 == test14);
}

template <typename T, typename M, typename S>
struct MemberSerializerInfo {
  M T::* member;
  S      serializer = DefaultSerializer<M>{};
};

template <typename T, typename M>
MemberSerializerInfo(M T::*) -> MemberSerializerInfo<T, M, DefaultSerializer<M>>;

template <typename T, auto creator, auto... members>
struct CustomSerializer {
  static void serialize(Pickle& pickle, T const& t) {
    // reverse order
    auto member_tuple = std::tuple{ members... };
    templateForEach<sizeof...(members)>([&]<size_t i, size_t index = sizeof...(members) - 1 - i>() {
      pickle.push(
        t.*std::get<index>(member_tuple).member, std::get<index>(member_tuple).serializer
      );
    });
  }
  static auto deserialize(Pickle& pickle) -> T {
    constexpr auto member_count = sizeof...(members);
    constexpr auto member_tuple = std::tuple{ members... };

    auto tuple = std::tuple{
      pickle.pop<
        std::remove_reference_t<decltype(std::declval<T>().*members.member)>,
        decltype(members.serializer)>()...,
    };
    return std::apply(
      [&](decltype(std::declval<T>().*members.member)&... args) {
        return creator(std::move(args)...);
      },
      tuple
    );
  }
};

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
};

struct TestSerializer : CustomSerializer<
                          Test,
                          [](int a, std::string b, std::vector<float> c, MoveOnly d) {
                            return Test{ a, b, std::move(c), std::move(d) };
                          },
                          MemberSerializerInfo{ &Test::a },
                          MemberSerializerInfo{ &Test::b },
                          MemberSerializerInfo{ &Test::c },
                          MemberSerializerInfo{ &Test::d }> {};

TEST(CustomSerializer) {
  auto pickle = Pickle{};
  auto test = Test{ 1, "2", { 3.0, 4.0, 5.0 }, 1 };
  pickle.push(test, TestSerializer{});
  pickle.dump("test.pkl");
  pickle = Pickle{ "test.pkl" };
  auto test2 = pickle.pop<Test, TestSerializer>();
  TOY_ASSERT(test.a == test2.a && test.b == test2.b && test.c == test2.c, test.d.aa == test2.d.aa);
}

class Persistent {
public:
  static auto generateGuid() -> std::string;
  template <typename T, typename Serializer>
  static void serialize(T t, std::string_view guid);
  template <typename T, typename Serializer>
  static T deserialize(std::string_view guid);
};

} // namespace toy
