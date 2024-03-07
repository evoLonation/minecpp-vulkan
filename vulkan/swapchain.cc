module vulkan.swapchain;

import "vulkan_config.h";
import vulkan.tool;
import vulkan.image;

import std;
import toy;

namespace vk {

auto createSurface(VkInstance instance, GLFWwindow* p_window) -> Surface {
  auto create_info = VkWin32SurfaceCreateInfoKHR{
    .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
    .hinstance = GetModuleHandle(nullptr),
    .hwnd = glfwGetWin32Window(p_window),
  };
  return { instance, create_info };
}

auto createSwapchain(
  VkSurfaceKHR                    surface,
  VkDevice                        device,
  const VkSurfaceCapabilitiesKHR& capabilities,
  VkSurfaceFormatKHR              surface_format,
  VkPresentModeKHR                present_mode,
  GLFWwindow*                     p_window,
  VkSwapchainKHR                  old_swapchain
) -> std::expected<std::pair<Swapchain, VkExtent2D>, SwapchainCreateError> {
  uint32_t image_count = capabilities.minImageCount + 1;
  // maxImageCount == 0意味着没有最大值
  if (capabilities.maxImageCount != 0) {
    image_count = std::min(image_count, capabilities.maxImageCount);
  }

  VkExtent2D extent{};
  // 一些窗口管理器确实允许我们在这里有所不同，这是通过将 currentExtent
  // 内的宽度和高度设置为一个特殊的值来表示的：uint32_t的最大值
  // currentExtent is the current width and height of the surface, or the
  // special value (0xFFFFFFFF, 0xFFFFFFFF) indicating that the surface size
  // will be determined by the extent of a swapchain targeting the surface
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    extent = capabilities.currentExtent;
  } else {
    int width, height;
    glfwGetFramebufferSize(p_window, &width, &height);
    extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    extent.width = std::clamp(
      extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width
    );
    extent.height = std::clamp(
      extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height
    );
  }
  if (extent.height == 0 || extent.width == 0) {
    return std::unexpected(SwapchainCreateError::EXTENT_ZERO);
  }

  auto create_info = VkSwapchainCreateInfoKHR{
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = surface,
    .minImageCount = image_count,
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

  toy::debugf("the info of created swap chain:");
  toy::debugf("image count:{}", image_count);
  toy::debugf("extent:({},{})", extent.width, extent.height);

  return std::pair{ Swapchain{ device, create_info }, extent };
}

auto createSwapchainImageViews(VkDevice device, VkSwapchainKHR swapchain, VkFormat format)
  -> std::vector<ImageView> {
  auto images = getVkResources(vkGetSwapchainImagesKHR, device, swapchain);
  return images | views::transform([device, format](VkImage image) {
           return createImageView(device, image, format);
         }) |
         ranges::to<std::vector>();
}

} // namespace vk