import toy;
// #include <test.h>
import std;
import glm;
#include <test.h>
#include <toy.h>

import component;

using namespace cp;

struct CustomComponent : Component {
  CustomComponent() { std::cout << "CustomComponent()" << std::endl; }
  CustomComponent(const CustomComponent&) noexcept = default;
  CustomComponent(CustomComponent&&) noexcept = default;
  auto operator=(const CustomComponent&) noexcept -> CustomComponent& = default;
  auto operator=(CustomComponent&&) noexcept -> CustomComponent& = default;
  ~CustomComponent() { std::cout << "~CustomComponent()" << std::endl; }
};
struct Custom2 : CustomComponent {};

TEST(transform) {

  auto cp = Custom2{};
  auto proxy = cp.getProxy();
  TOY_ASSERT(proxy.valid() && &proxy.get() == &cp);
  auto cp2 = std::move(cp);
  TOY_ASSERT(proxy.valid() && &proxy.get() == &cp2);
  auto cp3 = cp2;
  TOY_ASSERT(proxy.valid() && &proxy.get() == &cp2);
  auto proxy3 = cp3.getProxy();
  cp3 = std::move(cp2);
  TOY_ASSERT(proxy.valid() && &proxy.get() == &cp3);
  TOY_ASSERT(proxy3.valid() && &proxy3.get() == &cp3);

  auto proxy2 = proxy;
  TOY_ASSERT(&proxy2.get() == &proxy.get());
  proxy2 = std::move(proxy);
  TOY_ASSERT(!proxy.valid());
}