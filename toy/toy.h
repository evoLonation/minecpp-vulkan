#ifndef TOY_H
#define TOY_H

#define __NAME_TUPLE_STRING(...)                                                                   \
  toy::macro::nameTupleFormat({ #__VA_ARGS__ }, toy::macro::getTuple(__VA_ARGS__))
#define TOY_ASSERT(condition, ...)                                                                 \
  toy::throwf(condition, "assert error: " #condition ",\n  {}", __NAME_TUPLE_STRING(__VA_ARGS__))
#define TOY_CHECK(condition, ...)                                                                  \
  toy::checkf(condition, "check error: " #condition ",\n  {}", __NAME_TUPLE_STRING(__VA_ARGS__))

#define TOY_DEBUG(...) toy::debug(__NAME_TUPLE_STRING(__VA_ARGS__))

#endif