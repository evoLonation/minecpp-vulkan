module vulkan.swapchain;

import "vulkan_config.h";
import vulkan.tool;

import std;
import toy;

namespace ranges = std::ranges;
namespace views = std::views;

auto createSurface(VkInstance instance, GLFWwindow* p_window) -> VkSurfaceKHR {
  VkSurfaceKHR                surface;
  VkWin32SurfaceCreateInfoKHR createInfo{
    .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
    .hinstance = GetModuleHandle(nullptr),
    .hwnd = glfwGetWin32Window(p_window),
  };

  if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) !=
      VK_SUCCESS) {
    toy::throwf("failed to create surface!");
  }
  return surface;
}

void destroySurface(VkSurfaceKHR surface, VkInstance instance) noexcept {
  vkDestroySurfaceKHR(instance, surface, nullptr);
}

auto createSwapchain(VkSurfaceKHR                    surface,
                     VkDevice                        device,
                     const VkSurfaceCapabilitiesKHR& capabilities,
                     VkSurfaceFormatKHR              surface_format,
                     VkPresentModeKHR                present_mode,
                     GLFWwindow*                     p_window,
                     std::span<uint32_t>             queue_family_indices,
                     VkSwapchainKHR                  old_swapchain)
  -> std::expected<std::pair<VkSwapchainKHR, VkExtent2D>,
                   SwapchainCreateError> {
  uint32_t imageCount = capabilities.minImageCount + 1;
  // maxImageCount == 0意味着没有最大值
  if (capabilities.maxImageCount != 0) {
    imageCount = std::min(imageCount, capabilities.maxImageCount);
  }

  VkExtent2D extent{};
  // 一些窗口管理器确实允许我们在这里有所不同，这是通过将 currentExtent
  // 内的宽度和高度设置为一个特殊的值来表示的：uint32_t的最大值
  // currentExtent is the current width and height of the surface, or the
  // special value (0xFFFFFFFF, 0xFFFFFFFF) indicating that the surface size
  // will be determined by the extent of a swapchain targeting the surface
  if (capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
    extent = capabilities.currentExtent;
  } else {
    int width, height;
    glfwGetFramebufferSize(p_window, &width, &height);
    extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    extent.width = std::clamp(extent.width,
                              capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height,
                               capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
  }
  if (extent.height == 0 || extent.width == 0) {
    return std::unexpected(SwapchainCreateError::EXTENT_ZERO);
  }

  VkSwapchainCreateInfoKHR createInfo{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = surface,
    .minImageCount = imageCount,
    .imageFormat = surface_format.format,
    .imageColorSpace = surface_format.colorSpace,
    .imageExtent = extent,
    // 不整 3D 应用程序的话就设置为1
    .imageArrayLayers = 1,
    /*
     * VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT: 交换链的图像直接用于渲染
     * VK_IMAGE_USAGE_TRANSFER_DST_BIT :
     * 先渲染到单独的图像上（以便进行后处理），然后传输到交换链图像
     */
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .preTransform = capabilities.currentTransform,
    // alpha通道是否应用于与窗口系统中的其他窗口混合
    // 简单地忽略alpha通道
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = present_mode,
    // 不关心被遮挡的像素的颜色
    .clipped = VK_TRUE,
    // 尚且有效的swapchain，利于进行资源复用
    .oldSwapchain = old_swapchain,
  };

  /*
   * VK_SHARING_MODE_CONCURRENT:
   * 图像可以跨多个队列族使用，而无需明确的所有权转移
   * VK_SHARING_MODE_EXCLUSIVE:
   * 一个图像一次由一个队列族所有，在将其用于另一队列族之前，必须明确转移所有权
   * (性能最佳)
   */
  auto diff_indices =
    queue_family_indices | toy::chunkBy(std::equal_to{}) |
    views::transform([](auto subrange) { return *subrange.begin(); }) |
    ranges::to<std::vector>();
  if (diff_indices.size() >= 2) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = diff_indices.size();
    createInfo.pQueueFamilyIndices = diff_indices.data();
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = nullptr;
  }

  VkSwapchainKHR swapchain;

  if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create swap chain");
  }

  toy::debugf("the info of created swap chain:");
  toy::debugf("image count:{}", imageCount);
  toy::debugf("extent:({},{})", extent.width, extent.height);

  return std::pair{ swapchain, extent };
}

void destroySwapchain(VkSwapchainKHR swapchain, VkDevice device) noexcept {
  vkDestroySwapchainKHR(device, swapchain, nullptr);
}

auto createImageViews(VkDevice       device,
                      VkSwapchainKHR swapchain,
                      VkFormat       format) -> std::vector<VkImageView> {
  auto images = getVkResource(vkGetSwapchainImagesKHR, device, swapchain);
  auto image_views = images | views::transform([device, format](auto image) {
                       VkImageViewCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
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
        .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = 0,
          .levelCount = 1,
          .baseArrayLayer = 0,
          .layerCount = 1,
        },
      };
                       return createVkResource(
                         vkCreateImageView, "image view", device, &create_info);
                     }) |
                     ranges::to<std::vector>();
  return image_views;
}

void destroyImageViews(std::span<VkImageView> image_views,
                       VkDevice               device) noexcept {
  for (auto image_view : image_views) {
    vkDestroyImageView(device, image_view, nullptr);
  }
}
