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
    auto                  value() -> Enum& { return _value; }                                      \
    auto                  value() const -> const Enum& { return _value; }                          \
    auto                  str() const -> const char* { return &pair.first[pair.second[_value]]; }  \
    operator size_t() { return static_cast<size_t>(_value); }                                      \
    operator Enum() { return _value; }                                                             \
                                                                                                   \
  private:                                                                                         \
    Enum                  _value;                                                                  \
    static constexpr auto pair = toy::__splitString<__VA_ARGS__>(#__VA_ARGS__);                    \
  };