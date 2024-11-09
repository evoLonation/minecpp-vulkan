module render.sampler;

import <vulkan_config.h>;
import render.sync;
import render.executor;

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

decltype(SampledTexture::_formats) SampledTexture::_formats = { VK_FORMAT_R8G8B8A8_SRGB };

auto SampledTexture::create(const std::string& path, bool mipmap, VkPipelineStageFlagBits use_stage)
  -> SampledTexture {
  auto& ctx = Device::getInstance();

  // todo: just execute once in whole program
  _max_anisotropy = ctx.getPdevice().getProperties().limits.maxSamplerAnisotropy;

  uint32 width, height, channels;

  auto* pixels =
    stbi_load(path.data(), &(int&)width, &(int&)height, &(int&)channels, STBI_rgb_alpha);
  auto image_size = static_cast<VkDeviceSize>(width * height * 4);
  if (pixels == nullptr) {
    toy::throwf("failed to load image {}", path.data());
  }
  auto image_data = std::as_bytes(std::span{ pixels, image_size });
  toy::debugf("image {} info: width {}, height {}", path.data(), width, height);

  auto staging_buffer = StagingBuffer{ image_data };

  stbi_image_free(pixels);

  auto mip_extents = std::vector<VkExtent2D>{};
  auto mip_range = MipRange{
    .base_level = 0,
    .count = 1,
  };
  auto mip_levels = uint32{};
  if (mipmap) {
    mip_extents = computeMipExtents({ width, height });
    mip_levels = mip_extents.size();
    mip_range.count = mip_levels;
  }

  auto image = Image{
    _formats[0], width, height, _usage, _aspect, mip_levels, VK_SAMPLE_COUNT_1_BIT,
  };

  auto sampler = createSampler(_max_anisotropy);
  auto texture =
    SampledTexture{ std::move(staging_buffer), std::move(image), std::move(sampler), mip_range };
  auto& copy_executor = ExecutorManager::getInstance()[FamilyType::TRANSFER];
  auto& graphics_executor = ExecutorManager::getInstance()[FamilyType::GRAPHICS];
  auto  family_transfer =
    FamilyTransferInfo{ copy_executor.getFamily(), graphics_executor.getFamily() };

  auto recorder_copy = [&](VkCommandBuffer cmdbuf) {
    recordImageBarrier(
      cmdbuf,
      texture._image.getImage(),
      getSubresourceRange(_aspect, mip_range),
      { VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL },
      {
        Scope{
          .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        },
        Scope{
          .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
          .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
        },
      },
      {}
    );

    copyBufferToImage(
      cmdbuf,
      texture._staging_buffer,
      texture._image.getImage(),
      _aspect,
      { 0, 0 },
      { width, height },
      0
    );

    recordImageBarrier(
      cmdbuf,
      texture._image.getImage(),
      getSubresourceRange(_aspect, mip_range),
      {
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        mipmap ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      },
      BarrierScope::release(Scope{
        .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
      }),
      family_transfer
    );
  };

  auto recorder_blit = [&](VkCommandBuffer cmdbuf) {
    recordImageBarrier(
        cmdbuf,
        texture._image.getImage(),
        getSubresourceRange(_aspect, mip_range),
        {
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          mipmap ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        BarrierScope::acquire(mipmap ? Scope{
          .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
          .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
        }: Scope{
          .stage_mask = use_stage,
          .access_mask = VK_ACCESS_SHADER_READ_BIT,
        }),
        family_transfer
      );

    if (mipmap) {
      for (auto dst_mip_level : views::iota(1u, mip_extents.size())) {
        recordImageBarrier(
          cmdbuf,
          texture._image.getImage(),
          getSubresourceRange(_aspect, MipRange{ .base_level = dst_mip_level - 1, .count = 1 }),
          { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL },
          { Scope{
              .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
              .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
            },
            Scope{
              .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
              .access_mask = VK_ACCESS_TRANSFER_READ_BIT,
            } },
          {}
        );
        blitImage(
          cmdbuf,
          ImageBlit{
            .image = texture._image.getImage(),
            .aspect = _aspect,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .mip_level = dst_mip_level - 1,
            .extent = mip_extents[dst_mip_level - 1],
          },
          ImageBlit{
            .image = texture._image.getImage(),
            .aspect = _aspect,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .mip_level = dst_mip_level,
            .extent = mip_extents[dst_mip_level],
          }
        );
      }
      recordImageBarrier(
        cmdbuf,
        texture._image.getImage(),
        getSubresourceRange(_aspect, { .base_level = mip_levels - 1, .count = 1 }),
        { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { Scope{
            .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
          },
          Scope{
            .stage_mask = use_stage,
            .access_mask = VK_ACCESS_SHADER_READ_BIT,
          } },
        {}
      );
      recordImageBarrier(
        cmdbuf,
        texture._image.getImage(),
        getSubresourceRange(_aspect, { .base_level = 0, .count = mip_levels - 1 }),
        { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
        { Scope{
            .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .access_mask = 0,
          },
          Scope{
            .stage_mask = use_stage,
            .access_mask = VK_ACCESS_SHADER_READ_BIT,
          } },
        {}
      );
    }
  };

  auto waitable = copy_executor.submit(recorder_copy);
  graphics_executor.submit(CommandBatch{
    .recorder = recorder_blit,
    .waits = { { &waitable, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
  });
  texture.getImage().getTracker().setNewScope(
    Scope{
      .stage_mask = use_stage,
      .access_mask = VK_ACCESS_SHADER_READ_BIT,
    },
    family_transfer.dst_family,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  );
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