#define CHECK_VK_RESULT(result, ...)                                                               \
  rd::vk::checkVkResult(result, #result __VA_OPT__(, ) __VA_ARGS__);