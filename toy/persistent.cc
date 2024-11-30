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
  requires(std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>)
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
template <typename T>
  requires(std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>)
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

template <typename T>
struct SequenceSerializer {
  static void serialize(Pickle& pickle, T const& t) {
    for (auto const& e : t) {
      pickle.push(e);
    }
    pickle.push(static_cast<std::size_t>(t.size()));
  }
  static auto deserialize(Pickle& pickle) -> T {
    auto size = pickle.pop<std::size_t>();
    auto t = T{};
    t.resize(size);
    for (auto& e : t | views::reverse) {
      e = pickle.pop<typename T::value_type>();
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

TEST(SequenceSerializer) {
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
