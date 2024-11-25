module;
#include <toy.h>
module render.image;

import render.device;

namespace rd {

auto getSubresourceRange(VkImageAspectFlags aspect, MipRange mip_range) -> VkImageSubresourceRange {
  TOY_ASSERT(mip_range.count > 0);
  return {
    .aspectMask = aspect,
    .baseMipLevel = mip_range.base_level,
    .levelCount = mip_range.count,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };
}

auto getSubresourceLayers(VkImageAspectFlags aspect, uint32 mip_level) -> VkImageSubresourceLayers {
  return {
    .aspectMask = aspect,
    .mipLevel = mip_level,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };
}

auto computeMipExtents(VkExtent2D extent) -> std::vector<VkExtent2D> {
  auto mip_levels = uint32(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;
  auto mip_extents = std::vector<VkExtent2D>{};
  auto now_extent = extent;
  for (auto i : views::iota(0u, mip_levels)) {
    mip_extents.emplace_back(now_extent);
    now_extent = VkExtent2D{
      std::max(now_extent.width / 2, 1u),
      std::max(now_extent.height / 2, 1u),
    };
  }
  return mip_extents;
}

auto createImage(
  VkFormat              format,
  uint32                width,
  uint32                height,
  VkImageUsageFlags     usage,
  uint32                mip_levels,
  VkSampleCountFlagBits sample_count
) -> rs::Image {
  // if use for staging image, combine use:
  // VK_IMAGE_TILING_LINEAR, VK_IMAGE_LAYOUT_PREINITIALIZED,
  // VK_IMAGE_USAGE_TRANSFER_SRC_BIT
  auto image_info = VkImageCreateInfo{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = format,
    .extent =
      VkExtent3D{
        .width = width,
        .height = height,
        .depth = 1,
      },
    .mipLevels = mip_levels,
    .arrayLayers = 1,
    .samples = sample_count,
    // VK_IMAGE_TILING_LINEAR: Texels are laid out in row-major
    // order like our pixels array (almost no place to use it)
    // 想要直接访问 image 中的像素的话就用 VK_IMAGE_TILING_LINEAR
    // VK_IMAGE_TILING_OPTIMAL: Texels are laid out in an implementation
    // defined order for optimal access
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    // 仅当VK_SHARING_MODE_CONCURRENT时设置
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices = nullptr,
    // VK_IMAGE_LAYOUT_UNDEFINED: the contents of the data are considered to be undefined, and the
    // transition away from this layout is not guaranteed to preserve that data.
    // VK_IMAGE_LAYOUT_PREINITIALIZED: the image data can be preinitialized by the host while using
    // this layout, and the transition away from this layout will preserve that data.
    // For either of these initial layouts, any image subresources must be transitioned to another
    // layout before they are accessed by the device.
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  return { image_info };
}

auto createImageView(VkImage image, VkFormat format, uint32 mip_levels) -> rs::ImageView {
  auto create_info = VkImageViewCreateInfo{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext = nullptr,
    .flags = 0,
    .image = image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = format,
    // 颜色通道映射
    .components = {
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
    },
    // view 访问 image 资源的范围
    .subresourceRange = getSubresourceRange(getFormatAspect(format), {0, mip_levels}),
  };
  return { create_info };
}

ImageContext::ImageContext() {
  auto& properties = Device::getInstance().getPdevice().getProperties();
  _available_sample_counts = properties.limits.framebufferColorSampleCounts &
                             properties.limits.framebufferDepthSampleCounts &
                             properties.limits.framebufferStencilSampleCounts &
                             properties.limits.framebufferNoAttachmentsSampleCounts &
                             properties.limits.sampledImageColorSampleCounts &
                             properties.limits.sampledImageIntegerSampleCounts &
                             properties.limits.sampledImageDepthSampleCounts &
                             properties.limits.sampledImageStencilSampleCounts &
                             properties.limits.storageImageSampleCounts;
}

ImageResource::ImageResource(
  VkFormat              format,
  uint32                width,
  uint32                height,
  VkImageUsageFlags     usage,
  uint32                mip_levels,
  VkSampleCountFlagBits sample_count
)
  : _image(createImage(
      format,
      width,
      height,
      usage,
      mip_levels,
      [&]() {
        TOY_ASSERT(
          (ImageContext::getInstance().getAvailableSampleCounts() & sample_count) > 0, sample_count
        );
        return sample_count;
      }()
    )),
    _memory(_image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    _image_view(createImageView(_image, format, mip_levels)) {}

auto ImageResource::operator=(ImageResource&& e) noexcept -> ImageResource& {
  _image_view = std::move(e._image_view);
  _memory = std::move(e._memory);
  _image = std::move(e._image);
  return *this;
}

Image::Image(
  VkFormat              format,
  uint32                width,
  uint32                height,
  VkImageUsageFlags     usage,
  bool                  mipmap,
  VkSampleCountFlagBits sample_count
) {
  auto mipmap_extents = std::vector<VkExtent2D>{};
  auto mipmap_levels = 1u;
  if (mipmap) {
    mipmap_extents = computeMipExtents(VkExtent2D{ width, height });
    mipmap_levels = mipmap_extents.size();
  }
  ImageResource::operator=({ format, width, height, usage, mipmap_levels, sample_count });
  ImageManager::operator=({
    ImageResource::_image,
    ImageResource::_image_view,
    usage,
    VkExtent2D{ width, height },
    format,
    std::move(mipmap_extents),
    sample_count,
  });
}

auto Image::operator=(Image&& e) noexcept -> Image& {
  ImageManager::operator=(std::move(e));
  ImageResource::operator=(std::move(e));
  return *this;
}

} // namespace rd