#ifndef TOY_H
#define TOY_H

#define NAME_TUPLE_STRING(...)                                                                     \
  toy::macro::nameTupleFormat({ #__VA_ARGS__ }, toy::macro::getTuple(__VA_ARGS__))
#define TOY_ASSERT(condition, ...)                                                                 \
  toy::throwf(                                                                                     \
    static_cast<bool>(condition),                                                                  \
    "assert error: {} ,\n  {}",                                                                    \
    #condition,                                                                                    \
    NAME_TUPLE_STRING(__VA_ARGS__)                                                                 \
  )
#define TOY_CHECK(condition, ...)                                                                  \
  toy::checkf(condition, "check error: {} ,\n  {}", #condition, NAME_TUPLE_STRING(__VA_ARGS__))

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

#endif