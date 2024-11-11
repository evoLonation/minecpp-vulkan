#ifndef TOY_H
#define TOY_H

#define NAME_TUPLE_STRING(...)                                                                     \
  toy::macro::nameTupleFormat({ #__VA_ARGS__ }, toy::macro::getTuple(__VA_ARGS__))
#define TOY_ASSERT(condition, ...)                                                                 \
  toy::throwf(                                                                                     \
    static_cast<bool>(condition),                                                                  \
    "{}",                                                                                          \
    toy::macro::nextLineIfExist("assert error: " #condition, NAME_TUPLE_STRING(__VA_ARGS__))       \
  )
#define TOY_CHECK(condition, ...)                                                                  \
  toy::checkf(                                                                                     \
    condition,                                                                                     \
    "{}",                                                                                          \
    toy::macro::nextLineIfExist("check error: " #condition, NAME_TUPLE_STRING(__VA_ARGS__))        \
  )
#define TOY_CHECK_ASSERT(condition, ...)                                                           \
  do {                                                                                             \
    TOY_CHECK(condition, __VA_ARGS__);                                                             \
    TOY_ASSERT(condition, __VA_ARGS__);                                                            \
  } while (0)

#define TOY_DEBUG(...) toy::debug(NAME_TUPLE_STRING(__VA_ARGS__))

#define ONLY_MOVEABLE(type)                                                                        \
  type(const type&) noexcept = delete;                                                             \
  type(type&&) noexcept = default;                                                                 \
  auto operator=(const type&) noexcept -> type& = delete;                                          \
  auto operator=(type&&) noexcept -> type& = default;

#define UNCOPYABLE(type)                                                                           \
  type(const type&) noexcept = delete;                                                             \
  auto operator=(const type&) noexcept -> type& = delete;

#define UNMOVEABLE(type)                                                                           \
  type(type&&) noexcept = delete;                                                                  \
  auto operator=(type&&) noexcept -> type& = delete;

#define UNCOPYABLE_MOVEABLE(type)                                                                  \
  UNCOPYABLE(type)                                                                                 \
  UNMOVEABLE(type)

#define DEFAULT_MOVEABLE(type)                                                                     \
  type(type&&) noexcept = default;                                                                 \
  auto operator=(type&&) noexcept -> type& = default;

#define CUSTOM_FORMATTER(type, func)                                                               \
  export template <>                                                                               \
  class std::formatter<type> : public std::formatter<std::string> {                                \
  public:                                                                                          \
    template <typename FormatContext, typename... Args>                                            \
    auto format(const type& e, FormatContext& ctx) const {                                         \
      return std::formatter<std::string>::format(formatString(e), ctx);                            \
    }                                                                                              \
                                                                                                   \
  private:                                                                                         \
    auto formatString(const type& e) const -> std::string { return func(e); }                      \
  };

#define PROACTIVE_SINGLETON(cls) toy::ProactiveSingleton<cls, #cls>

#endif