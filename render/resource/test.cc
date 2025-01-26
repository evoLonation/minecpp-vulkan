import toy;
import std;
import render.reflections;
import render.format;
import <vulkan_config.h>;
#include <test.h>
#include <toy.h>

TEST(format) {
  using namespace rd;
  constexpr auto a = getFormatInfo(VK_FORMAT_D32_SFLOAT);
  if (a.type == COLOR) {
    auto color = a.color;
    toy::debugf("{}", std::tuple{ color.component_size, (int)color.components, (int)color.scalar });
  } else {
    auto depst = a.depst;
    toy::debugf(
      "{}",
      std::tuple{
        depst.depth_size, (int)depst.depth_scalar, depst.stencil_size, (int)depst.stencil_scalar }
    );
  }
}