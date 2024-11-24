module;
#include <toy.h>
module render.copy;

import <vulkan_config.h>;

namespace rd {

// must only one bit of aspect (by specification)
auto getBufferImageCopy(
  VkImageAspectFlagBits aspect, uint32 mip_level, VkOffset2D offset, VkExtent2D extent
) -> VkBufferImageCopy {
  return VkBufferImageCopy{
    .bufferOffset = 0,
    // bufferRowLength and bufferImageHeight
    // 用于更详细的定义buffer的内存如何映射到image
    .bufferRowLength = 0,
    .bufferImageHeight = 0,
    .imageSubresource = getSubresourceLayers(aspect, mip_level),
    .imageOffset =
      VkOffset3D{
        .x = offset.x,
        .y = offset.y,
        .z = 0,
      },
    .imageExtent =
      VkExtent3D{
        .width = extent.width,
        .height = extent.height,
        .depth = 1,
      },
  };
}

void copyBufferToImage(
  VkCommandBuffer       cmdbuf,
  VkBuffer              buffer,
  VkImage               image,
  VkImageAspectFlagBits aspect,
  VkOffset2D            offset,
  VkExtent2D            extent,
  uint32                mip_level
) {
  auto image_copy = getBufferImageCopy(aspect, mip_level, offset, extent);
  vkCmdCopyBufferToImage(
    cmdbuf, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy
  );
}

void copyImageToBuffer(
  VkCommandBuffer       cmdbuf,
  VkImage               image,
  VkBuffer              buffer,
  VkImageAspectFlagBits aspect,
  VkOffset2D            offset,
  VkExtent2D            extent,
  uint32                mip_level
) {
  auto image_copy = getBufferImageCopy(aspect, mip_level, offset, extent);
  vkCmdCopyImageToBuffer(
    cmdbuf, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &image_copy
  );
}

void blitImage(VkCommandBuffer cmdbuf, ImageBlit src, ImageBlit dst) {
  auto blit = VkImageBlit{
    .srcSubresource = getSubresourceLayers(src.aspect, src.mip_level),
    .srcOffsets = { VkOffset3D{ 0, 0, 0 },
                    VkOffset3D{ (int32)src.extent.width, (int32)src.extent.height, 1 } },
    .dstSubresource = getSubresourceLayers(dst.aspect, dst.mip_level),
    .dstOffsets = { VkOffset3D{ 0, 0, 0 },
                    VkOffset3D{ (int32)dst.extent.width, (int32)dst.extent.height, 1 } },
  };
  vkCmdBlitImage(cmdbuf, src.image, src.layout, dst.image, dst.layout, 1, &blit, VK_FILTER_LINEAR);
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

/**
 * @brief
 * @param max_extent the max sub range read from image
 */
ImageLocalReader::ImageLocalReader(VkFormat format, VkExtent2D max_extent)
  : _format(format), _max_extent(max_extent) {
  _buffer = HostBuffer{
    getFormatSize(format) * max_extent.width * max_extent.height,
    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
  };
}

void ImageLocalReader::loadImage(
  ImageManager&         manager,
  VkImageAspectFlagBits aspect,
  VkOffset2D            offset,
  VkExtent2D            extent,
  uint32                mip_level
) {
  TOY_ASSERT(manager.getUsage() & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
  TOY_ASSERT(_format == manager.getFormat());
  TOY_ASSERT(extent.width <= _max_extent.width && extent.height <= _max_extent.height);
  TOY_ASSERT(manager.getSampleCount() == VK_SAMPLE_COUNT_1_BIT);

  auto& executor = ExecutorManager::getInstance()[FamilyType::TRANSFER];

  auto batches = std::vector<CommandBatch>{};
  auto waitable_keep_lifetime = std::list<Waitable>{};
  auto syncDealer = [&](SyncContext sync) {
    if (auto* recorder = std::get_if<BarrierRecorder>(&sync)) {
      batches.push_back(CommandBatch{ std::move(*recorder) });
    } else if (auto* recorder = std::get_if<FamilyTransferRecorder>(&sync)) {
      auto waitable = recorder->executeRelease();
      waitable_keep_lifetime.push_back(std::move(waitable));
      batches.push_back(recorder->toAcquireBatch(&waitable_keep_lifetime.back()));
    }
  };
  syncDealer(manager.getTracker().syncScope(
    Scope{
      .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .access_mask = VK_ACCESS_TRANSFER_READ_BIT,
    },
    executor.getFamily(),
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
  ));
  syncDealer(_buffer.getTracker().syncScope(
    Scope{
      .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
    },
    executor.getFamily()
  ));
  batches.push_back(CommandBatch{ [&](VkCommandBuffer cmdbuf) {
    copyImageToBuffer(cmdbuf, manager.getImage(), _buffer.get(), aspect, offset, extent, mip_level);
  } });
  // need make device access is available to host access
  syncDealer(_buffer.getTracker().syncScope(
    Scope{
      .stage_mask = VK_PIPELINE_STAGE_HOST_BIT,
      .access_mask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT,
    },
    executor.getFamily()
  ));
  executor.submit(batches).back().wait();
  _buffer.getTracker().clearScope();
  _local_extent = extent;
}

auto ImageLocalReader::readPixel(uint32 x, uint32 y) -> std::byte* {
  auto unit = getFormatSize(_format);
  return _buffer.getMemory().data().begin().base() + _local_extent.width * unit * y + unit * x;
}

} // namespace rd