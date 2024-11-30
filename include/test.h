#ifndef TEST_H
#define TEST_H

#define BEGIN_TEST_DECLARE__(id) std::vector<std::pair<std::string_view, void (*)()>> id##_list;
#define BEGIN_TEST_DECLARE_(id) BEGIN_TEST_DECLARE__(id)
#define BEGIN_TEST BEGIN_TEST_DECLARE_(TEST_IDENTITY)

#define TEST__(id, name)                                                                           \
  void test_##name();                                                                              \
  auto test_loader_##name = []() {                                                                 \
    id##_list.push_back({ #name, test_##name });                                                   \
    return 0;                                                                                      \
  }();                                                                                             \
  void test_##name()

#define TEST_(id, name) TEST__(id, name)
#define TEST(name) TEST_(TEST_IDENTITY, name)

BEGIN_TEST

#endif