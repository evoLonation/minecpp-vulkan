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

#endif