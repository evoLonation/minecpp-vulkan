module render.vk.swapchain;

import "vulkan_config.h";
import render.vk.surface;
import render.vk.format;

import std;
import toy;

namespace rd::vk {

Swapchain::Swapchain(uint32_t min_image_count) {
  updateCapabilities();
  _swapchain_extent = _capabilities.currentExtent;
  if (min_image_count == 0) {
    if (_capabilities.maxImageCount == _capabilities.minImageCount) {
      _min_image_count = _capabilities.minImageCount;
    } else {
      _min_image_count = _capabilities.minImageCount + 1;
    }
  } else if (_capabilities.maxImageCount != 0) {
    // maxImageCount == 0意味着没有最大值
    _min_image_count = std::min(min_image_count, _capabilities.maxImageCount);
  }
  create();
}

void Swapchain::create() {

  if (_swapchain_extent.height == 0 || _swapchain_extent.width == 0) {
    _swapchain = {};
    _images = {};
  }

  auto create_info = VkSwapchainCreateInfoKHR{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = Surface::getInstance(),
    .minImageCount = _min_image_count,
    .imageFormat = _format,
    .imageColorSpace = _color_space,
    .imageExtent = _swapchain_extent,
    // 不整 3D 应用程序的话就设置为1
    .imageArrayLayers = 1,
    /*
     * VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT: 交换链的图像直接用于渲染
     * VK_IMAGE_USAGE_TRANSFER_DST_BIT :
     * 先渲染到单独的图像上（以便进行后处理），然后传输到交换链图像
     */
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    /*
     * VK_SHARING_MODE_CONCURRENT:
     * 图像可以跨多个队列族使用，而无需明确的所有权转移
     * VK_SHARING_MODE_EXCLUSIVE:
     * 一个图像一次由一个队列族所有，在将其用于另一队列族之前，必须明确转移所有权
     * (性能最佳)
     */
    .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices = nullptr,

    .preTransform = _capabilities.currentTransform,
    // alpha通道是否应用于与窗口系统中的其他窗口混合
    // 简单地忽略alpha通道
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = _present_mode,
    // 不关心被遮挡的像素的颜色
    .clipped = VK_TRUE,
    // 尚且有效的swapchain，利于进行资源复用
    .oldSwapchain = _swapchain.get(),
  };

  toy::debugf("the info of created swap chain:");
  toy::debugf("min image count:{}", _min_image_count);
  toy::debugf("extent:({},{})", _swapchain_extent.width, _swapchain_extent.height);

  auto& device = Device::getInstance();

  _swapchain = rs::Swapchain{ device, create_info };
  _images = getVkResources(vkGetSwapchainImagesKHR, device, _swapchain);
}

auto Swapchain::needRecreate() -> bool {
  updateCapabilities();
  auto extent = _capabilities.currentExtent;
  return !(extent.height == _swapchain_extent.height && extent.width == _swapchain_extent.width);
}

void Swapchain::recreate() {
  _swapchain_extent = _capabilities.currentExtent;
  create();
}

auto Swapchain::checkPdevice(const PdeviceContext& ctx) -> bool {
  auto& surface = Surface::getInstance();
  auto  formats = getVkResources(vkGetPhysicalDeviceSurfaceFormatsKHR, ctx.device, surface);
  auto  iter_format = ranges::find_if(formats, [&](auto format) {
    return format.format == _format && format.colorSpace == _color_space;
  });
  if (iter_format == formats.end()) {
    toy::debugf("no suitable format");
    return false;
  }
  if (!ctx.checkFormatSupport(
        FormatTarget::OPTIMAL_TILING,
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
        { &_format, &_format + 1 }
      )) {
    return false;
  }
  /*
   * VK_PRESENT_MODE_IMMEDIATE_KHR: 图像提交后直接渲染到屏幕上
   * VK_PRESENT_MODE_FIFO_KHR:
   * 有一个队列，队列以刷新率的速度消耗图像显示在屏幕上，图像提交后入队，队列满时等待（也即只能在
   * "vertical blank" 时刻提交图像）
   * VK_PRESENT_MODE_FIFO_RELAXED_KHR: 当图像提交时，若队列为空，就直接渲染到屏幕上，否则同上
   * VK_PRESENT_MODE_MAILBOX_KHR: 有一个 single-entry queue, 当队列满时,
   * 不阻塞而是直接将队中图像替换为提交的图像
   */
  auto present_modes =
    getVkResources(vkGetPhysicalDeviceSurfacePresentModesKHR, ctx.device, surface);
  auto p_present_mode = ranges::find(present_modes, _present_mode);
  if (p_present_mode == present_modes.end()) {
    toy::debugf("no suitable present mode");
    return false;
  }
  return true;
}

void Swapchain::updateCapabilities() {
  // With Win32, minImageExtent, maxImageExtent, and currentExtent must always equal the window
  // size.
  // The currentExtent of a Win32 surface must have both width and height greater than 0, or both
  // of them 0.
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    Device::getInstance().pdevice(), Surface::getInstance(), &capabilities
  );
  auto eq_extent = [](auto a, auto b) { return a.height == b.height && a.width == b.width; };
  toy::throwf(
    eq_extent(capabilities.currentExtent, capabilities.minImageExtent) &&
      eq_extent(capabilities.currentExtent, capabilities.maxImageExtent),
    "minImageExtent, maxImageExtent, and currentExtent must always equal."
  );
  _capabilities = capabilities;
}

} // namespace rd::vk