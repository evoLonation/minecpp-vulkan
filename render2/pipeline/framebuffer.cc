module;
#include <toy.h>
module render.framebuffer;

import <vulkan_config.h>;

namespace rd {

void FrameImageManager::beforeDestroy() {
  if (valid()) {
    getTracker().waitIdle();
    FramebufferPool::getInstance().destroyRelativeFramebuf(*this);
  }
}

auto FramebufferPool::getFramebuffer(
  VkRenderPass render_pass, std::span<FrameImageManager* const> images
) -> std::pair<VkFramebuffer, VkExtent2D> {
  check();
  auto extent = images[0]->getExtent();
  auto eq_extent = [](auto x, auto y) { return x.width == y.width && x.height == y.height; };
  TOY_ASSERT(ranges::all_of(images, [&](auto x) { return eq_extent(x->getExtent(), extent); }));
  auto image_views = std::vector<VkImageView>{};
  for (auto* image : images) {
    image_views.push_back(image->getImageView());
  }
  if (auto it = _image_views2framebuffer.find(image_views); it != _image_views2framebuffer.end()) {
    return { it->second, extent };
  }
  check();
  auto framebuffer = rs::Framebuffer{ VkFramebufferCreateInfo{
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass = render_pass,
    .attachmentCount = static_cast<uint32>(image_views.size()),
    .pAttachments = image_views.data(),
    .width = extent.width,
    .height = extent.height,
    .layers = 1,
  } };
  auto handle = framebuffer.get();
  _framebuffers.emplace(handle, std::move(framebuffer));
  auto image_views_ptr =
    &_image_views2framebuffer.emplace(std::move(image_views), handle).first->first;
  _framebuffer2image_views.emplace(handle, image_views_ptr);
  for (auto image_view : *image_views_ptr) {
    _image_view2framebuffers[image_view].insert(handle);
  }
  _counter2framebuffer.emplace(_use_counter, handle);
  _framebuffer2counter.emplace(handle, _use_counter);
  _use_counter++;
  if (_framebuffers.size() > _max_capacity) {
    auto framebuffer = _counter2framebuffer.extract(_counter2framebuffer.begin()).mapped();
    _framebuffer2counter.erase(framebuffer);
    _framebuffers.erase(framebuffer);
    auto image_views_ptr = _framebuffer2image_views.extract(framebuffer).mapped();
    for (auto image_view : *image_views_ptr) {
      _image_view2framebuffers[image_view].erase(framebuffer);
    }
    _image_views2framebuffer.erase(*image_views_ptr);
  }
  check();
  return { handle, extent };
}

void FramebufferPool::check(std::source_location loc) {
  // toy::debugf(loc, "framebuffers: {}", _framebuffers.size());
  TOY_CHECK(
    _image_views2framebuffer.size() == _framebuffers.size(),
    _image_views2framebuffer.size(),
    _framebuffers.size(),
    loc
  );
  TOY_CHECK(
    _framebuffer2image_views.size() == _framebuffers.size(),
    _framebuffer2image_views.size(),
    _framebuffers.size(),
    loc
  );
  TOY_CHECK(
    _counter2framebuffer.size() == _framebuffers.size(),
    _counter2framebuffer.size(),
    _framebuffers.size(),
    loc
  );
  TOY_CHECK(
    _framebuffer2counter.size() == _framebuffers.size(),
    _framebuffer2counter.size(),
    _framebuffers.size(),
    loc
  );
  TOY_CHECK(
    (views::join(_image_view2framebuffers | views::values) | ranges::to<std::set>()).size() ==
      _framebuffers.size(),
    (views::join(_image_view2framebuffers | views::values) | ranges::to<std::set>()).size(),
    _framebuffers.size(),
    loc
  );
}

/**
 * @brief must ensure the relative framebuffer is already idle
 */
void FramebufferPool::destroyRelativeFramebuf(FrameImageManager& image) {
  check();
  if (auto it = _image_view2framebuffers.find(image.getImageView());
      it != _image_view2framebuffers.end()) {
    auto framebuffers = _image_view2framebuffers.extract(it).mapped();
    for (auto framebuffer : framebuffers) {
      _framebuffers.erase(framebuffer);
      auto* image_views_ptr = _framebuffer2image_views.extract(framebuffer).mapped();
      for (auto image_view : *image_views_ptr) {
        _image_view2framebuffers[image_view].erase(framebuffer);
      }
      _image_views2framebuffer.erase(*image_views_ptr);
      _counter2framebuffer.erase(_framebuffer2counter.extract(framebuffer).mapped());
      _framebuffer2counter.erase(framebuffer);
    }
  }
  check();
}

} // namespace rd