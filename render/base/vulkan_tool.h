#define CHECK_VK_RESULT(result, ...)                                                               \
  rd::checkVkResult(result, #result __VA_OPT__(, ) __VA_ARGS__);