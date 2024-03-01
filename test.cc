// import std;
#include <iostream>
struct A {
  int         a;
  friend auto operator<=>(const A& a, const A& b) -> std::strong_ordering
  // = default;
  {
    return a.a <=> b.a;
  }
};
template <typename T>
void                         bar(const T& a) {}
typedef struct VkInstance_T* VkInstance;
int                          foo() {
  // auto aa = std::tuple{1, 2, 3};
  // auto& [a, b, c] {aa};
  // decltype(a) d;
  float x{};
  char  y{};
  int   z{};

  std::tuple<float&, char&&, int> tpl(x, std::move(y), z);
  const auto& [a, b, c] = tpl;
  // a指名指代x的结构化绑定；decltype(a)为float&
  // b指名指代y的结构化绑定；decltype(b)为char&&
  // c指名指代z的结构化绑定；decltype(c)为const int，注意有const
}