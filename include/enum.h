#define TOY_ENUM(name, ...)                                                                        \
  class name : public toy::EnumBase {                                                              \
    template <typename EnumT>                                                                      \
    friend constexpr auto toy::enum2String(EnumT t) -> const char*;                                \
                                                                                                   \
  public:                                                                                          \
    enum Enum { __VA_ARGS__ };                                                                     \
    constexpr name(Enum value) : _value(value) {}                                                  \
    constexpr name(size_t value) : _value(static_cast<Enum>(value)) {}                             \
    constexpr name() = default;                                                                    \
    static constexpr auto count = [](auto... values) { return sizeof...(values); }(__VA_ARGS__);   \
    constexpr auto        value() -> Enum& { return _value; }                                      \
    constexpr auto        value() const -> const Enum& { return _value; }                          \
    constexpr auto        str() const -> const char* { return &pair.first[pair.second[_value]]; }  \
    constexpr             operator size_t() { return static_cast<size_t>(_value); }                \
    constexpr             operator Enum() { return _value; }                                       \
    friend constexpr auto operator==(name a, name b) {                                             \
      return static_cast<size_t>(a) == static_cast<size_t>(b);                                     \
    }                                                                                              \
    friend constexpr auto operator==(name a, name::Enum b) {                                       \
      return static_cast<size_t>(a) == static_cast<size_t>(b);                                     \
    }                                                                                              \
    friend constexpr auto operator==(name::Enum a, name b) {                                       \
      return static_cast<size_t>(a) == static_cast<size_t>(b);                                     \
    }                                                                                              \
                                                                                                   \
  private:                                                                                         \
    Enum                  _value;                                                                  \
    static constexpr auto pair = toy::__splitString<__VA_ARGS__>(#__VA_ARGS__);                    \
  };