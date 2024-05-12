#define TOY_ENUM(name, ...)                                                                        \
  class name {                                                                                     \
    template <typename EnumT>                                                                      \
    friend constexpr auto toy::enum2String(EnumT t) -> const char*;                                \
                                                                                                   \
  public:                                                                                          \
    enum Enum { __VA_ARGS__ };                                                                     \
    name(Enum value) : _value(value) {}                                                            \
    name(size_t value) : _value(static_cast<Enum>(value)) {}                                       \
    name() = default;                                                                              \
    static constexpr auto count = [](auto... values) { return sizeof...(values); }(__VA_ARGS__);   \
    auto                  value() -> Enum { return _value; }                                       \
                                                                                                   \
  private:                                                                                         \
    Enum                  _value;                                                                  \
    static constexpr auto pair = toy::__splitString<__VA_ARGS__>(#__VA_ARGS__);                    \
  };