module;
#include <toy.h>
module render.sampler;

import <vulkan_config.h>;
import render.sync;
import render.executor;
import render.copy;

import <stb_image.h>;

namespace rd {

auto createSampler(float max_anisotropy) -> rs::Sampler {
  toy::debugf("max_anisotropy: {}", max_anisotropy);
  // lod 是 lod 等级，用于选择纹理过滤模式等等
  // level 是在 lod 基础上计算得到的 mip 等级
  // lod = clamp(lod_base + mipLodBias, minLod, maxLod)
  // level = (baseMipLevel + clamp(lod, 0, levelCount - 1))
  auto sampler_info = VkSamplerCreateInfo{
    .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    // VK_FILTER_NEAREST and VK_FILTER_LINEAR, 插值模式
    // 分别是 lod <= 0 和 lod > 0 时采用的纹理过滤模式
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    // VK_SAMPLER_MIPMAP_MODE_NEAREST: 将 level 四舍五入后选择对应的mip等级
    // VK_SAMPLER_MIPMAP_MODE_LINEAR: 根据 level 在两个mip等级之间线性插值
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    // VK_SAMPLER_ADDRESS_MODE_REPEAT：超出图像尺寸时重复纹理。
    // VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT：同上，但是镜像图像。
    // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE：使最接近坐标的边缘的颜色超出图像尺寸。
    // VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE：同上，但使用对面的边
    // VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER：采样超出尺寸时返回纯色
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    .mipLodBias = 0.0f,
    // 各向异性过滤
    .anisotropyEnable = VK_TRUE,
    .maxAnisotropy = max_anisotropy,
    .compareEnable = VK_FALSE,
    .compareOp = VK_COMPARE_OP_ALWAYS,
    .minLod = 0.0f,
    // VK_LOD_CLAMP_NONE is a special value for maxLod to indicate that not to clamp maxLod
    .maxLod = VK_LOD_CLAMP_NONE,
    .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
    // VK_FALSE: (0, 1)寻址， 反之 (0, width), (0, height)寻址
    .unnormalizedCoordinates = VK_FALSE,
  };
  return { sampler_info };
}

decltype(SampledTexture::_formats) SampledTexture::_formats = {
  VK_FORMAT_R8G8B8A8_SRGB,
  VK_FORMAT_R32G32B32A32_SFLOAT,
};

SampledTexture::SampledTexture(
  std::span<std::byte const> data,
  VkFormat                   format,
  VkExtent2D                 extent,
  bool                       mipmap,
  VkPipelineStageFlagBits    use_stage
)
  : DescriptorResource{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER } {
  TOY_ASSERT(toy::find(_formats, format));
  // todo: just execute once in whole program
  _max_anisotropy = Device::getInstance().getPdevice().getProperties().limits.maxSamplerAnisotropy;
  auto [width, height] = extent;
  _writer = ImageLocalWriter{ format, extent };
  _image = Image{
    format, width, height, _usage, mipmap, VK_SAMPLE_COUNT_1_BIT,
  };
  _sampler = createSampler(_max_anisotropy);
  _writer.writeImage(
    _image,
    _aspect,
    data,
    Scope{ use_stage, VK_ACCESS_SHADER_READ_BIT },
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  );
}

auto SampledTexture::fromFile(
  const std::string& path, bool mipmap, VkPipelineStageFlagBits use_stage
) -> SampledTexture {
  uint32 width, height, channels;
  auto*  pixels =
    stbi_load(path.data(), &(int&)width, &(int&)height, &(int&)channels, STBI_rgb_alpha);
  auto image_size = static_cast<VkDeviceSize>(width * height * 4);
  if (pixels == nullptr) {
    toy::throwf("failed to load image {}", path.data());
  }
  auto image_data = std::as_bytes(std::span{ pixels, image_size });
  toy::debugf("image {} info: width {}, height {}", path.data(), width, height);

  auto texture =
    SampledTexture{ image_data, VK_FORMAT_R8G8B8A8_SRGB, { width, height }, mipmap, use_stage };

  stbi_image_free(pixels);
  return texture;
}

auto SampledTexture::getDescriptorContext() -> Context {
  return ImageContext{
    .dscriptor_info =
      VkDescriptorImageInfo{
        .sampler = getSampler(),
        .imageView = getImage().getImageView(),
        .imageLayout = getLayout(),
      },
    .tracker = &_image.getTracker(),
  };
}

auto SampledTexture::checkPdevice(DeviceCapabilityBuilder& request) -> bool {
  if (!request.enableFeature(&VkPhysicalDeviceFeatures::samplerAnisotropy)) {
    return false;
  }
  return request.getPdevice().checkFormatSupport(
    FormatTarget::OPTIMAL_TILING,
    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
      VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT,
    _formats
  );
}

}; // namespace rd