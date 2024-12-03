import toy;
import std;
import render.reflections;
import <vulkan_config.h>;
#include <test.h>
#include <toy.h>

namespace rd {

enum ScalarType {
  SFLOAT,
  SINT,
  UINT,
  UNORM,
  SNORM,
  SRGB,
};

enum ComponentType {
  R,
  RG,
  RGB,
  RGBA,
  BGRA,
};

struct ColorFormatInfo {
  std::array<uint32, 4> rgba_sizes;
  ComponentType         components;
  ScalarType            scalar;
};

struct DepstFormatInfo {
  uint32     depth_size;
  ScalarType depth_scalar;
  uint32     stencil_size;
  ScalarType stencil_scalar;
};

struct FormatInfo {
  bool is_color;
  union {
    ColorFormatInfo color;
    DepstFormatInfo depst;
  } info;
};

constexpr auto getFormatInfo(VkFormat format) -> FormatInfo {
  auto str = refl::format(format);
  using namespace std::string_literals;
  str = str.substr("VK_FORMAT_"s.size());
  auto forwardCompare = [](std::string_view& str, auto str2) -> bool {
    if (str.starts_with(str2)) {
      if constexpr (std::is_same_v<decltype(str2), char>) {
        str = str.substr(1);
      } else {
        str = str.substr(std::string_view{ str2 }.size());
      }
      return true;
    } else {
      return false;
    }
  };
  auto skip = [&]() {
    while (str.size() && str[0] == '_') {
      str = str.substr(1);
    }
  };
  auto getSize = [&]() -> uint32 {
    skip();
    if (forwardCompare(str, "8")) {
      return 8;
    } else if (forwardCompare(str, "16")) {
      return 16;
    } else if (forwardCompare(str, "24")) {
      return 24;
    } else if (forwardCompare(str, "32")) {
      return 32;
    } else if (forwardCompare(str, "64")) {
      return 64;
    }
    toy::throwf("unsupported format: {}", format);
  };
  auto getScalarType = [&]() -> ScalarType {
    skip();
    using enum ScalarType;
    if (forwardCompare(str, "SFLOAT")) {
      return SFLOAT;
    } else if (forwardCompare(str, "SINT")) {
      return SINT;
    } else if (forwardCompare(str, "UINT")) {
      return ScalarType::UINT;
    } else if (forwardCompare(str, "UNORM")) {
      return UNORM;
    } else if (forwardCompare(str, "SNORM")) {
      return SNORM;
    } else if (forwardCompare(str, "SRGB")) {
      return SRGB;
    }
    toy::throwf("unsupported format: {}", format);
  };
  auto compareWith = [&](char c) -> bool {
    skip();
    return forwardCompare(str, c);
  };
  if (compareWith('D')) {
    auto depth_size = getSize();
    auto depth_type = getScalarType();
    if (str.size() == 0) {
      auto info = DepstFormatInfo{
        .depth_size = depth_size,
        .depth_scalar = depth_type,
      };
      return { .is_color = false, .info = { .depst = info } };
    } else {
      TOY_ASSERT(compareWith('S'));
      auto stencil_size = getSize();
      auto stencil_type = getScalarType();
      auto info = DepstFormatInfo{
        .depth_size = depth_size,
        .depth_scalar = depth_type,
        .stencil_size = stencil_size,
        .stencil_scalar = stencil_type,
      };
      return { .is_color = false, .info = { .depst = info } };
    }
  } else if (compareWith('S')) {
    auto stencil_size = getSize();
    auto stencil_type = getScalarType();
    auto info = DepstFormatInfo{
      .stencil_size = stencil_size,
      .stencil_scalar = stencil_type,
    };
    return { .is_color = false, .info = { .depst = info } };
  } else if (compareWith('R')) {
    uint32 r_size{}, g_size{}, b_size{}, a_size{};
    r_size = getSize();
    auto component = ComponentType::R;
    if (compareWith('G')) {
      g_size = getSize();
      component = ComponentType::RG;
      if (compareWith('B')) {
        b_size = getSize();
        component = ComponentType::RGB;
        if (compareWith('A')) {
          a_size = getSize();
          component = ComponentType::RGBA;
        }
      }
    }
    auto type = getScalarType();
    auto info = ColorFormatInfo{
      .rgba_sizes = { r_size, g_size, b_size, a_size },
      .components = component,
      .scalar = type,
    };
    return { .is_color = true, .info = { .color = info } };
  } else if (compareWith('B')) {
    uint32 b_size = getSize();
    TOY_ASSERT(compareWith('G'));
    uint32 g_size = getSize();
    TOY_ASSERT(compareWith('R'));
    uint32 r_size = getSize();
    TOY_ASSERT(compareWith('A'));
    uint32 a_size = getSize();

    auto type = getScalarType();
    auto info = ColorFormatInfo{
      .rgba_sizes = { r_size, g_size, b_size, a_size },
      .components = ComponentType::BGRA,
      .scalar = type,
    };
    return { .is_color = true, .info = { .color = info } };
  }
  toy::throwf("unsupported format");
}

} // namespace rd

TEST(format) {
  using namespace rd;
  constexpr auto a = getFormatInfo(VK_FORMAT_D32_SFLOAT);
  if (a.is_color) {
    auto color = a.info.color;
    toy::debugf(
      "{}", std::tuple{ color.rgba_sizes, (int)color.components, (int)color.scalar }
    );
  } else {
    auto depst = a.info.depst;
    toy::debugf(
      "{}",
      std::tuple{
        depst.depth_size, (int)depst.depth_scalar, depst.stencil_size, (int)depst.stencil_scalar }
    );
  }
}