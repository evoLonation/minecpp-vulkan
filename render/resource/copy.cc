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

void copyImageToImage(
  VkCommandBuffer    cmdbuf,
  VkImage            src_image,
  VkImage            dst_image,
  VkImageAspectFlags aspect,
  VkOffset2D         src_offset,
  VkOffset2D         dst_offset,
  VkExtent2D         extent
) {
  auto image_copy = VkImageCopy{
    .srcSubresource = getSubresourceLayers(aspect, 0),
    .srcOffset = VkOffset3D{ src_offset.x, src_offset.y, 0 },
    .dstSubresource = getSubresourceLayers(aspect, 0),
    .dstOffset = VkOffset3D{ dst_offset.x, dst_offset.y, 0 },
    .extent = VkExtent3D{ .width = extent.width, .height = extent.height, .depth = 1 },
  };
  vkCmdCopyImage(
    cmdbuf,
    src_image,
    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    dst_image,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1,
    &image_copy
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

BufferLocalReader::BufferLocalReader(VkDeviceSize max_size) {
  _buffer = HostBuffer{ max_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT };
}

void BufferLocalReader::loadBuffer(Buffer& buffer, VkDeviceSize offset, VkDeviceSize size) {
  TOY_ASSERT(size <= _buffer.size());
  TOY_ASSERT(offset + size <= buffer.size());
  TOY_ASSERT(buffer.getUsage() & VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

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
  syncDealer(buffer.getTracker().syncScope(
    Scope{ VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT }, executor.getFamily()
  ));
  syncDealer(_buffer.getTracker().syncScope(
    Scope{ VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT }, executor.getFamily()
  ));
  batches.push_back(CommandBatch{ [&](VkCommandBuffer cmdbuf) {
    copyBuffer(cmdbuf, buffer.get(), _buffer.get(), offset, 0, size);
  } });
  // need make device access is available to host access
  syncDealer(_buffer.getTracker().syncScope(
    Scope{ VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT }, executor.getFamily()
  ));
  executor.submit(batches).back().wait();
  _buffer.getTracker().clearScope();
  _local_size = size;
}

auto BufferLocalReader::readData(VkDeviceSize offset, VkDeviceSize size) -> std::span<std::byte> {
  TOY_ASSERT(offset + size <= _local_size);
  return _buffer.getMemory().data().subspan(offset, size);
}

/**
 * @brief
 * @param max_extent the max sub range read from image
 */
ImageLocalReader::ImageLocalReader(VkFormat format, VkExtent2D max_extent)
  : _format(format), _max_extent(max_extent) {
  _buffer = HostBuffer{
    getFormatInfo(format).size * max_extent.width * max_extent.height,
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
  TOY_ASSERT(manager.getAspect() & aspect);

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
  auto unit = getFormatInfo(_format).size;
  return _buffer.getMemory().data().begin().base() + _local_extent.width * unit * y + unit * x;
}

ImageLocalWriter::ImageLocalWriter(VkFormat format, VkExtent2D max_extent) {
  _format = format;
  _max_extent = max_extent;
  TOY_DEBUG(_max_extent.width, _max_extent.height);
  _buffer = HostBuffer{
    getFormatInfo(format).size * max_extent.width * max_extent.height,
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
  };
}

void ImageLocalWriter::writeImage(
  ImageManager&              image,
  VkImageAspectFlagBits      aspect,
  std::span<std::byte const> data,
  Scope                      dst_scope,
  VkImageLayout              dst_layout
) {
  TOY_ASSERT(image.getUsage() & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  TOY_ASSERT(image.getFormat() == _format);
  TOY_ASSERT(
    data.size() ==
    getFormatInfo(image.getFormat()).size * image.getExtent().width * image.getExtent().height
  );
  TOY_ASSERT(data.size() <= _buffer.size(), data.size(), _buffer.size());
  TOY_ASSERT(image.getSampleCount() == VK_SAMPLE_COUNT_1_BIT);

  auto& copy_executor = ExecutorManager::getInstance()[FamilyType::TRANSFER];
  auto& graphics_executor = ExecutorManager::getInstance()[FamilyType::GRAPHICS];
  auto  family_transfer =
    FamilyTransferInfo{ copy_executor.getFamily(), graphics_executor.getFamily() };
  auto mip_range = MipRange{
    .base_level = 0,
    .count = 1,
  };
  auto mip_levels = 1u;
  if (image.enableMipmap()) {
    mip_range.count = image.getMipmapExtents().size();
    mip_levels = mip_range.count;
  }
  auto recorder_copy = [&](VkCommandBuffer cmdbuf) {
    copyBufferToImage(cmdbuf, _buffer, image.getImage(), aspect, { 0, 0 }, image.getExtent(), 0);

    recordImageBarrier(
      cmdbuf,
      image.getImage(),
      getSubresourceRange(image.getAspect(), mip_range),
      {
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        image.enableMipmap() ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : dst_layout,
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
        image.getImage(),
        getSubresourceRange(image.getAspect(), mip_range),
        {
          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          image.enableMipmap() ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : dst_layout,
        },
        BarrierScope::acquire(image.enableMipmap() ? Scope{
          .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
          .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
        }: dst_scope),
        family_transfer
      );

    if (image.enableMipmap()) {
      for (auto dst_mip_level : views::iota(1u, image.getMipmapExtents().size())) {
        recordImageBarrier(
          cmdbuf,
          image.getImage(),
          getSubresourceRange(
            image.getAspect(), MipRange{ .base_level = dst_mip_level - 1, .count = 1 }
          ),
          { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL },
          { Scope{ VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT },
            Scope{ VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT } },
          {}
        );
        blitImage(
          cmdbuf,
          ImageBlit{
            .image = image.getImage(),
            .aspect = aspect,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .mip_level = dst_mip_level - 1,
            .extent = image.getMipmapExtents()[dst_mip_level - 1],
          },
          ImageBlit{
            .image = image.getImage(),
            .aspect = aspect,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .mip_level = dst_mip_level,
            .extent = image.getMipmapExtents()[dst_mip_level],
          }
        );
      }
      recordImageBarrier(
        cmdbuf,
        image.getImage(),
        getSubresourceRange(image.getAspect(), { .base_level = mip_levels - 1, .count = 1 }),
        { VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dst_layout },
        { Scope{ VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT }, dst_scope },
        {}
      );
      if (mip_levels > 1) {
        recordImageBarrier(
          cmdbuf,
          image.getImage(),
          getSubresourceRange(image.getAspect(), { .base_level = 0, .count = mip_levels - 1 }),
          { VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst_layout },
          { Scope{ VK_PIPELINE_STAGE_TRANSFER_BIT, 0 }, dst_scope },
          {}
        );
      }
    }
  };

  // _buffer is read only for host, so just wait for idle, no need to sync
  _buffer.getTracker().waitIdle();
  _buffer.getMemory().fill(data);

  auto recorder_copy_with_sync = std::function<void(VkCommandBuffer)>{};
  auto sync = image.getTracker().syncScope(
    Scope{
      .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
    },
    copy_executor.getFamily(),
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
  );
  auto waitable_opt = std::optional<Waitable>{};
  if (auto* recorder = std::get_if<BarrierRecorder>(&sync)) {
    recorder_copy_with_sync = [recorder, &recorder_copy](VkCommandBuffer cmdbuf) {
      (*recorder)(cmdbuf);
      recorder_copy(cmdbuf);
    };
  } else if (auto* recorder = std::get_if<FamilyTransferRecorder>(&sync)) {
    waitable_opt = recorder->executeRelease();
    recorder_copy_with_sync = [recorder, &recorder_copy](VkCommandBuffer cmdbuf) {
      recorder->acquire(cmdbuf);
      recorder_copy(cmdbuf);
    };
  } else {
    recorder_copy_with_sync = recorder_copy;
  }
  auto copy_batch = CommandBatch{ .recorder = std::move(recorder_copy_with_sync) };
  if (waitable_opt) {
    copy_batch.waits = { { &*waitable_opt, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } };
  }
  auto waitable = copy_executor.submit(copy_batch);
  graphics_executor.submit(CommandBatch{
    .recorder = recorder_blit,
    .waits = { { &waitable, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } },
  });
  image.getTracker().setNewScope(dst_scope, family_transfer.dst_family, dst_layout);
  _buffer.getTracker().setNewScope(
    Scope{ VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT }, copy_executor.getFamily()
  );
}

} // namespace rd