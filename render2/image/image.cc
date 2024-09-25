module render.vk.image;

import render.vk.resource;
import render.vk.device;

namespace rd::vk {

auto getSubresourceRange(VkImageAspectFlags aspect, MipRange mip_range) -> VkImageSubresourceRange {
  return {
    .aspectMask = aspect,
    .baseMipLevel = mip_range.base_level,
    .levelCount = mip_range.count,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };
}

auto getSubresourceLayers(VkImageAspectFlags aspect, uint32_t mip_level)
  -> VkImageSubresourceLayers {
  return {
    .aspectMask = aspect,
    .mipLevel = mip_level,
    .baseArrayLayer = 0,
    .layerCount = 1,
  };
}

auto createImage(
  VkFormat              format,
  uint32_t              width,
  uint32_t              height,
  VkImageUsageFlags     usage,
  uint32_t              mip_levels,
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

auto createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t mip_levels)
  -> rs::ImageView {
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
    .subresourceRange = getSubresourceRange(aspect, {0, mip_levels}),
  };
  return { create_info };
}

Image::Image(
  VkFormat              format,
  uint32_t              width,
  uint32_t              height,
  VkImageUsageFlags     usage,
  VkImageAspectFlags    aspect,
  uint32_t              mip_levels,
  VkSampleCountFlagBits sample_count
)
  : rs::Image(createImage(
      format,
      width,
      height,
      usage,
      mip_levels,
      [&]() {
        toy::throwf(
          (getAvailableSampleCounts() & sample_count) > 0,
          "the sample count {} is not supported",
          uint32_t(sample_count)
        );
        return sample_count;
      }()
    )),
    _memory(get(), VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    _image_view(createImageView(get(), format, aspect, mip_levels)) {}
auto Image::getAvailableSampleCounts() -> VkSampleCountFlags {
  if (_sample_counts == 0) {
    auto& properties = Device::getInstance().getPdevice().getProperties();
    _sample_counts = properties.limits.framebufferColorSampleCounts &
                     properties.limits.framebufferDepthSampleCounts &
                     properties.limits.framebufferStencilSampleCounts &
                     properties.limits.framebufferNoAttachmentsSampleCounts &
                     properties.limits.sampledImageColorSampleCounts &
                     properties.limits.sampledImageIntegerSampleCounts &
                     properties.limits.sampledImageDepthSampleCounts &
                     properties.limits.sampledImageStencilSampleCounts &
                     properties.limits.storageImageSampleCounts;
  }
  return _sample_counts;
}

void copyBufferToImage(
  VkCommandBuffer       cmdbuf,
  VkBuffer              buffer,
  VkImage               image,
  VkImageAspectFlagBits aspect,
  uint32_t              width,
  uint32_t              height,
  uint32_t              mip_level
) {
  auto image_copy = VkBufferImageCopy{
    .bufferOffset = 0,
    // bufferRowLength and bufferImageHeight
    // 用于更详细的定义buffer的内存如何映射到image
    .bufferRowLength = 0,
    .bufferImageHeight = 0,
    .imageSubresource = getSubresourceLayers(aspect, mip_level),
    .imageOffset =
      VkOffset3D{
        .x = 0,
        .y = 0,
        .z = 0,
      },
    .imageExtent =
      VkExtent3D{
        .width = (uint32_t)width,
        .height = (uint32_t)height,
        .depth = 1,
      },
  };
  vkCmdCopyBufferToImage(
    cmdbuf, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy
  );
}

void blitImage(VkCommandBuffer cmdbuf, ImageBlit src, ImageBlit dst) {
  auto blit = VkImageBlit{
    .srcSubresource = getSubresourceLayers(src.aspect, src.mip_level),
    .srcOffsets = { VkOffset3D{ 0, 0, 0 },
                    VkOffset3D{ (int32_t)src.extent.width, (int32_t)src.extent.height, 1 } },
    .dstSubresource = getSubresourceLayers(dst.aspect, dst.mip_level),
    .dstOffsets = { VkOffset3D{ 0, 0, 0 },
                    VkOffset3D{ (int32_t)dst.extent.width, (int32_t)dst.extent.height, 1 } },
  };
  vkCmdBlitImage(cmdbuf, src.image, src.layout, dst.image, dst.layout, 1, &blit, VK_FILTER_LINEAR);
}

auto computeMipExtents(VkExtent2D extent) -> std::vector<VkExtent2D> {
  auto mip_levels = uint32_t(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;
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

} // namespace rd::vk