module;
#include "vulkan_config.h"

module vulkan;

import std;
import tool;
import log;

void checkGlfwError() {
  const char *description;
  auto errors = views::repeat(description) |
                views::take_while([&description](auto _) {
                  return glfwGetError(&description) != GLFW_NO_ERROR;
                });
  if (!errors.empty()) {
    throwf("{::}", errors);
  }
}

auto createWindow(uint32_t width, uint32_t height, std::string_view title)
    -> GLFWwindow * {
  try {
    if (glfwInit() != GLFW_TRUE) {
      throw std::runtime_error("glfw init failed");
    }
    // 不要创建openGL上下文
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // 禁用改变窗口尺寸
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    auto p_window =
      glfwCreateWindow(width, height, title.data(), nullptr, nullptr);

    checkGlfwError();
    return p_window;
  } catch (const std::exception &) {
    glfwTerminate();
    throw;
  }
}

void destroyWindow(GLFWwindow *p_window) noexcept {
  if (p_window != nullptr) {
    glfwDestroyWindow(p_window);
  }
  glfwTerminate();
}

template <bool enable_valid_layer>
auto createInstanceTemplate(
    std::string_view appName, PFN_vkDebugUtilsMessengerCallbackEXT callback,
    DebugMessengerInfo* p_debug_messenger_info) {
  /*
   * 1. 创建appInfo
   * 2. 创建createInfo（指向appInfo）
   * 3. 调用createInstance创建instance
   */
  // 大多数 info 结构体的 sType 成员都需要显式声明
  // create 函数忽略allocator参数
  // 选择开启验证层

  VkApplicationInfo app_info{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = appName.data(),
      .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
      .engineVersion = VK_MAKE_VERSION(1, 0, 0),
      .apiVersion = VK_API_VERSION_1_0,
  };

  const auto requried_extensions =
      getRequiredInstanceExtensions<enable_valid_layer>();
  const auto requires_layers = getRequiredLayers<enable_valid_layer>();

  VkInstanceCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = static_cast<uint32_t>(requires_layers.size()),
      .ppEnabledLayerNames = requires_layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(requried_extensions.size()),
      .ppEnabledExtensionNames = requried_extensions.data(),
  };

  VkInstance instance;
  auto instance_creator = [&create_info, &instance]() {
    if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) {
      throwf("failed to create vulkan instance");
    }
  };

  if constexpr (enable_valid_layer) {
    VkDebugUtilsMessengerEXT debug_messenger;
    /*
     * VkDebugUtilsMessengerCreateInfoEXT: 设置要回调的消息类型、回调函数指针
     */
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = callback,
        .pUserData = p_debug_messenger_info};
    create_info.pNext = &debug_messenger_create_info;

    instance_creator();

    if (createDebugUtilsMessengerEXT(instance, &debug_messenger_create_info,
                                     nullptr, &debug_messenger) != VK_SUCCESS) {
      throwf("failed to create debug messenger");
    }

    return std::pair{instance, debug_messenger};
  } else {
    instance_creator();
    return instance;
  }
}

auto createInstance(std::string_view app_name) -> VkInstance {
	return createInstanceTemplate<false>(app_name, nullptr, nullptr);
}
auto createInstanceAndDebugMessenger(
    std::string_view app_name, PFN_vkDebugUtilsMessengerCallbackEXT callback,
    DebugMessengerInfo& debug_messenger_info)
    -> std::pair<VkInstance, VkDebugUtilsMessengerEXT> {
	return createInstanceTemplate<true>(app_name, callback, &debug_messenger_info);
}

void destroyInstance(VkInstance instance) noexcept {
	vkDestroyInstance(instance, nullptr);
}
void destroyDebugMessenger(VkDebugUtilsMessengerEXT debug_messenger,
                           VkInstance instance) noexcept {
  destroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
}

auto createDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger) -> VkResult {
  if (const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
      func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void destroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator) 
{
  if (const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
      func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

template <bool enable_valid_layer>
auto getRequiredInstanceExtensions() -> std::vector<const char *> {
  // glfw 需要的扩展用于 vulkan 与窗口对接
	// VK_EXT_debug_utils 扩展用于扩展debug功能
	uint32_t glfw_required_count;
  const auto glfw_required =
      glfwGetRequiredInstanceExtensions(&glfw_required_count);

  std::vector<const char *> required_extensions{
      glfw_required, glfw_required + glfw_required_count};

  if constexpr (enable_valid_layer) {
    required_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  checkAvaliableSupports(
      required_extensions,
      getVkResource(vkEnumerateInstanceExtensionProperties, nullptr),
      "extensions", [](auto &extension) { return extension.extensionName; });

  return required_extensions;
}

template <bool enabla_valid_layer>
auto getRequiredLayers() -> std::vector<const char *> {
  std::vector<const char *> required_layers;
  if constexpr (enabla_valid_layer) {
    required_layers.push_back("VK_LAYER_KHRONOS_validation");
  }

  checkAvaliableSupports(required_layers,
                         getVkResource(vkEnumerateInstanceLayerProperties),
                         "layers", [](auto &layer) { return layer.layerName; });

  return required_layers;
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debugHandler(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
             VkDebugUtilsMessageTypeFlagsEXT message_type,
             const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data,
             void *p_user_data) {
  auto& info = *reinterpret_cast<DebugMessengerInfo*>(p_user_data);
  /*
   * VkDebugUtilsMessageSeverityFlagBitsEXT : 严重性， VERBOSE, INFO, WARNING,
   * ERROR (可以比较，越严重越大） VkDebugUtilsMessageTypeFlagsEXT : 类型，
   * GENERAL, VALIDATION, PERFORMANCE return: 是否要中止 触发验证层消息 的
   * vulkan调用， 应该始终返回VK_FALSE
   */
  if (message_severity < info.message_severity_level)
    return VK_FALSE;
  if (!(message_type & info.message_type_flags))
    return VK_FALSE;
  auto serverityGetter = [](VkDebugUtilsMessageSeverityFlagBitsEXT e) {
    switch (e) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
      return "VERBOSE";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
      return "INFO";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
      return "WARNING";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
      return "ERROR";
    default:
      return "OTHER";
    }
  };
  auto typeGetter = [](VkDebugUtilsMessageTypeFlagsEXT e) {
    switch (e) {
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
      return "GENERAL";
    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
      return "VALIDATION";
    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
      return "PERFORMANCE";
    case VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT:
      return "DEVICE_ADDRESS_BINDING";
    default:
      return "OTHER";
    }
  };
  debugf("validation layer: ({},{}) {}", serverityGetter(message_severity),
         typeGetter(message_type), p_callback_data->pMessage);

  return VK_FALSE;
}

auto createSurface(VkInstance instance, GLFWwindow *p_window) -> VkSurfaceKHR {
  VkSurfaceKHR surface;
  VkWin32SurfaceCreateInfoKHR createInfo{
      .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
      .hinstance = GetModuleHandle(nullptr),
      .hwnd = glfwGetWin32Window(p_window),
  };

  if (vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) !=
      VK_SUCCESS) {
    throwf("failed to create surface!");
  }
  return surface;
}

void destroySurface(VkSurfaceKHR surface, VkInstance instance) noexcept {
  vkDestroySurfaceKHR(instance, surface, nullptr);
}

auto pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface,
                        std::span<const char *> required_extensions,
                        DeviceChecker device_checker,
                        SurfaceChecker surface_checker,
                        std::span<QueueFamilyChecker> queue_chekers)
    -> PhysicalDeviceInfo {
  std::vector<VkPhysicalDevice> devices =
      getVkResource(vkEnumeratePhysicalDevices, instance);
  std::vector<PhysicalDeviceInfo> supported_devices;
  for (auto device : devices) {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device, &device_properties);
    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(device, &device_features);
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities);
    auto presentModes = getVkResource(vkGetPhysicalDeviceSurfacePresentModesKHR,
                                      device, surface);
    auto formats =
        getVkResource(vkGetPhysicalDeviceSurfaceFormatsKHR, device, surface);

    debugf("checking physical device {}:", device_properties.deviceName);
    bool unsupport = false;
    if (!device_checker(
            DeviceCheckContext{device_properties, device_features})) {
      unsupport = true;
    }
    auto selected_surface = surface_checker(
        SurfaceCheckContext{capabilities, presentModes, formats});
    if (!selected_surface.has_value()) {
      unsupport = true;
    }

    try {
      checkAvaliableSupports(
          required_extensions,
          getVkResource(vkEnumerateDeviceExtensionProperties, device, nullptr),
          "device extensions",
          [](auto &extension) { return extension.extensionName; });
    } catch (const std::exception &e) {
      debug(e.what());
      unsupport = true;
    }

    auto queue_family_indices =
        getQueueFamilyIndices(device, surface, queue_chekers);
    if (!queue_family_indices.has_value()) {
      unsupport = true;
    }

    if (!unsupport) {
      supported_devices.push_back(
          {.device = device,
           .properties = device_properties,
           .features = device_features,
           .capabilities = capabilities,
           .present_mode = selected_surface->present_mode,
           .surface_format = selected_surface->surface_format,
           .queue_indices = queue_family_indices.value()});
    }
  }
  if (supported_devices.empty()) {
    throwf("no support physical device");
  }
  debugf("support devices: {::}",
         supported_devices | views::transform([](auto &info) {
           return info.properties.deviceName;
         }));
  auto selected_device = supported_devices[0];
  debugf("select device {}", selected_device.properties.deviceName);
  return selected_device;
}

auto getQueueFamilyIndices(VkPhysicalDevice device, VkSurfaceKHR surface,
                           std::span<QueueFamilyChecker> queue_chekers)
    -> std::optional<QueueIndexes> {

  auto queue_families =
      getVkResource(vkGetPhysicalDeviceQueueFamilyProperties, device);

  auto family_size = queue_families.size();
  auto request_size = queue_chekers.size();
	debugf("queue family size: {}, queue request size: {}", family_size, request_size);

  // 二分图，左半边是所有请求，右半边是所有队列(将队列族展开)
  std::vector<int> visit(request_size);
	std::fill(visit.begin(), visit.end(), -1);

  std::vector<std::vector<int>> queue2request(family_size);
  for (auto &&[properties, vector] : ranges::zip_view(queue_families, queue2request)) {
    vector = std::vector<int>(properties.queueCount);
    std::fill(vector.begin(), vector.end(), -1);
  }
  QueueIndexes request2family(request_size);

  std::vector<std::vector<std::pair<int, int>>> graph(request_size);

  for (auto [family_i, properties] : queue_families | enumerate) {
    int request_i = 0;
    int queue_number = properties.queueCount;
    debugf("check queue family {}, which has {} queues", family_i,
           queue_number);
    for (auto &&[request_i, queue_checker] : queue_chekers | enumerate) {
      if (queue_checker(
              QueueFamilyCheckContext{device, surface, family_i, properties})) {
        graph[request_i].append_range(
            views::zip(views::repeat(family_i, queue_number),
                       views::iota(0, queue_number)));
      } else {
        debugf("queue request {} failed", request_i);
      }
    }
  }

  std::function<bool(int, int)> dfs = [&graph, &visit, &queue2request,
                                       &request2family,
                                       &dfs](int u, int tag) -> bool {
		debugf("u: {}", u);
    if (visit[u] == tag) {
			debugf("visit[u] == tag, return false");
      return false;
    }
    visit[u] = tag;
    for (auto [family_i, queue_i] : graph[u]) {
			debugf("u {} lookup {}", u, std::pair{family_i, queue_i});
      if ((queue2request[family_i][queue_i] == -1 ||
           dfs(queue2request[family_i][queue_i], tag))) {
        queue2request[family_i][queue_i] = u;
        request2family[u] = {family_i, queue_i};
				debugf("u {} select {}", u, std::pair{family_i, queue_i});
        return true;
      }
    }
		debugf("u {} no satisfied select", u);
    return false;
  };
	
  if (!ranges::all_of(views::iota((decltype(request_size))0, request_size),
                      [&dfs](int u) { return dfs(u, u); })) {
    return std::nullopt;
  }

  return request2family;
}

auto checkPhysicalDeviceSupport(const DeviceCheckContext &ctx) -> bool {
  return check_debugf(
             ctx.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
             "device not satisfied VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU") &&
         check_debugf(ctx.features.geometryShader == VK_TRUE,
                      "device not support geometryShader feature");
}

auto checkSurfaceSupport(const SurfaceCheckContext &ctx)
    -> std::optional<SelectedSurfaceInfo> {
  auto p_format = ranges::find_if(ctx.surface_formats, [](auto format) {
    return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
           format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  });
  if (p_format == ctx.surface_formats.end()) {
    debugf("no suitable format");
    return std::nullopt;
  }
  /*
   * VK_PRESENT_MODE_IMMEDIATE_KHR: 图像提交后直接渲染到屏幕上
   * VK_PRESENT_MODE_FIFO_KHR:
   * 有一个队列，队列以刷新率的速度消耗图像显示在屏幕上，图像提交后入队，队列满时等待（也即只能在
   * "vertical blank" 时刻提交图像） VK_PRESENT_MODE_FIFO_RELAXED_KHR:
   * 当图像提交时，若队列为空，就直接渲染到屏幕上，否则同上
   * VK_PRESENT_MODE_MAILBOX_KHR:
   * 当队列满时，不阻塞而是直接将队中图像替换为已提交的图像
   */
  auto p_present_mode =
      ranges::find(ctx.present_modes, VK_PRESENT_MODE_FIFO_KHR);
  if (p_present_mode == ctx.present_modes.end()) {
    debugf("no suitable present mode");
    return std::nullopt;
  }
  return {{*p_present_mode, *p_format}};
}

auto checkGraphicQueue(const QueueFamilyCheckContext &ctx) -> bool {
  return check_debugf(
      ctx.properties.queueFlags & VK_QUEUE_GRAPHICS_BIT,
      "can not found queue family which satisfied VK_QUEUE_GRAPHICS_BIT");
}
auto checkPresentQueue(const QueueFamilyCheckContext &ctx) -> bool {
  VkBool32 presentSupport = false;
  vkGetPhysicalDeviceSurfaceSupportKHR(ctx.device, ctx.index, ctx.surface,
                                       &presentSupport);
  return check_debugf(
      presentSupport == VK_TRUE,
      "can not found queue family which satisfied SurfaceSupport");
}

auto createLogicalDevice(
    const PhysicalDeviceInfo &physical_device_info,
    std::span<const char *> required_extensions)
    -> std::pair<VkDevice, std::vector<VkQueue>> {
  /*
   * 1. 创建 VkDeviceQueueCreateInfo 数组, 用于指定 logic device 中的队列
   * 2. 根据 queue create info 和 deviceFeatures_ 创建 device create info
   *
   */

  auto create_meta_infos =
      SortedView(physical_device_info.queue_indices |
                 views::transform(&std::pair<uint32_t, uint32_t>::first)) |
      chunkBy(std::equal_to{}) | views::transform([](auto subrange) {
        std::vector<float> queue_priorities(subrange.size());
        ranges::fill(queue_priorities, 1.0);
        return std::tuple{*subrange.begin(),
                          static_cast<uint32_t>(subrange.size()),
                          std::move(queue_priorities)};
      }) |
      ranges::to<std::vector>();
  std::vector<VkDeviceQueueCreateInfo> queue_create_infos =
      create_meta_infos | views::transform([](auto &&tuple) {
        return VkDeviceQueueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = std::get<0>(tuple),
            .queueCount = std::get<1>(tuple),
            .pQueuePriorities = std::get<2>(tuple).data(),
        };
      }) |
      ranges::to<std::vector>();

  VkDeviceCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
      .pQueueCreateInfos = queue_create_infos.data(),
      .enabledExtensionCount =
          static_cast<uint32_t>(required_extensions.size()),
      .ppEnabledExtensionNames = required_extensions.data(),
      .pEnabledFeatures = &physical_device_info.features,
  };

  // 旧实现在 (instance, physical device) 和 (logic device以上) 两个层面有不同的
  // layer, 而在新实现中合并了 不再需要定义 enabledLayerCount 和
  // ppEnabledLayerNames createInfo.enabledLayerCount =
  // static_cast<uint32_t>(requiredLayers_.size());
  // createInfo.ppEnabledLayerNames = requiredLayers_.data();

  VkDevice device;

  if (vkCreateDevice(physical_device_info.device, &create_info, nullptr,
                     &device) != VK_SUCCESS) {
    throw std::runtime_error("failed to create logical device!");
  }

  auto queues = physical_device_info.queue_indices |
                views::transform([device](auto pair) {
                  VkQueue queue;
                  vkGetDeviceQueue(device, pair.first, pair.second, &queue);
                  return queue;
                }) | ranges::to<std::vector>();
  return {device, queues};
}

void destroyLogicalDevice(VkDevice device) noexcept {
  vkDestroyDevice(device, nullptr);
}

auto createSwapChain(VkSurfaceKHR surface, VkDevice device,
                     VkSurfaceCapabilitiesKHR capabilities,
                     VkSurfaceFormatKHR surface_format,
                     VkPresentModeKHR present_mode, GLFWwindow *p_window,
                     std::span<uint32_t> queue_family_indices)
    -> std::pair<VkSwapchainKHR, VkExtent2D> {
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
    extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height =
        std::clamp(extent.height, capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);
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
      .oldSwapchain = VK_NULL_HANDLE,
  };

  /*
   * VK_SHARING_MODE_CONCURRENT:
   * 图像可以跨多个队列族使用，而无需明确的所有权转移
   * VK_SHARING_MODE_EXCLUSIVE:
   * 一个图像一次由一个队列族所有，在将其用于另一队列族之前，必须明确转移所有权
   * (性能最佳)
   */
  auto diff_indices =
      queue_family_indices | chunkBy(std::equal_to{}) |
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

  debugf("the info of created swap chain:");
  debugf("image count:{}", imageCount);
  debugf("extent:({},{})", extent.width, extent.height);

  return {swapchain, extent};
}

void destroySwapChain(VkSwapchainKHR swapchain, VkDevice device) noexcept {
  vkDestroySwapchainKHR(device, swapchain, nullptr);
}

auto createImageViews(VkDevice device, VkSwapchainKHR swapchain,
                      VkFormat format) -> std::vector<VkImageView> {
  auto images = getVkResource(vkGetSwapchainImagesKHR, device, swapchain);
  auto image_views = images | views::transform(
    [device, format](auto image) {
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
      return createVkResource(vkCreateImageView, "image view", device, &create_info);
    }) |
    ranges::to<std::vector>();
  return image_views;
}

void destroyImageViews(std::span<VkImageView> image_views,
                       VkDevice device) noexcept {
  for (auto image_view : image_views) {
    vkDestroyImageView(device, image_view, nullptr);
  }
}

auto createRenderPass(VkDevice device, VkFormat format) -> VkRenderPass {
  VkAttachmentDescription color_attachment {
    .format = format,
    .samples =  VK_SAMPLE_COUNT_1_BIT,
    // VK_ATTACHMENT_LOAD_OP_LOAD: 保留 attachment 中现有内容
    // VK_ATTACHMENT_LOAD_OP_CLEAR: 将其中内容清理为一个常量
    // VK_ATTACHMENT_LOAD_OP_DONT_CARE: 不在乎
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    // VK_ATTACHMENT_STORE_OP_STORE: 渲染后内容存入内存稍后使用
    // VK_ATTACHMENT_STORE_DONT_CARE: 不在乎
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,

    .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    // 开启及结束时 要求 的图像布局
    .initialLayout =  VK_IMAGE_LAYOUT_UNDEFINED,
    .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
  };
  VkAttachmentReference color_attachment_ref {
    // 引用的 attachment 的索引
    .attachment = 0,
    // 用到该 ref 的 subpass 过程中使用的布局，会自动转换
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  VkSubpassDescription subpass {
    // 还有 compute、 ray tracing 等等
    .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
    .colorAttachmentCount = 1,
    // 这里的数组的索引和 着色器里的 layout 数值一一对应
    .pColorAttachments = &color_attachment_ref,
    // pInputAttachments: Attachments that are read from a shader
    // pResolveAttachments: Attachments used for multisampling color attachments
    // pDepthStencilAttachment: Attachment for depth and stencil data
    // pPreserveAttachments: Attachments that are not used by this subpass, but for which the data must be preserved
  };
  // attachment 的 layout 转换是在定义的依赖的中间进行的
  // 如果不主动定义从 VK_SUBPASS_EXTERNAL 到 第一个使用attachment的subpass 的dependency
  // 就会隐式定义一个，VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT到VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
  VkSubpassDependency dependency {
    // VK_SUBPASS_EXTERNAL 代表整个render pass之前提交的命令
    // 而vkQueueSubmit中设置的semaphore wait operation就是 renderpass 之前提交的
    // 但是提交的这个命令的执行阶段是在VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT阶段
    .srcSubpass = VK_SUBPASS_EXTERNAL,
    .dstSubpass = 0,
    .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = 0,
    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
  };
  VkRenderPassCreateInfo render_pass_create_info {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &color_attachment,
    .subpassCount = 1,
    .pSubpasses = &subpass,
    .dependencyCount = 1,
    .pDependencies = &dependency,
  };

  return createVkResource(vkCreateRenderPass, "render pass", device,
                          &render_pass_create_info);
}

void destroyRenderPass(VkRenderPass render_pass, VkDevice device) noexcept {
  vkDestroyRenderPass(device, render_pass, nullptr);
}

auto createShaderModule(std::string_view filepath, VkDevice device) -> VkShaderModule {
  std::ifstream istrm {filepath, std::ios::in | std::ios::binary};
  if (!istrm.is_open()){
    throwf("Open shader file {} failed!", filepath);
  }
  std::vector<byte> content;
  std::copy(std::istreambuf_iterator<char>{istrm}, std::istreambuf_iterator<char>{},
            std::back_inserter(content));
  VkShaderModuleCreateInfo create_info{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = content.size(),
    .pCode = reinterpret_cast<uint32_t*>(content.data()),
  };
  return createVkResource(vkCreateShaderModule, "shader module", device, &create_info);
}

auto createGraphicsPipeline(VkDevice device, VkRenderPass render_pass)
    -> PipelineResource {
  constexpr bool enable_blending_color = false;
  auto vertex_shader = createShaderModule("vert.spv", device);
  auto frag_shader = createShaderModule("frag.spv", device);
  // pSpecializationInfo 可以为 管道 配置着色器的常量，利于编译器优化，类似 constexpr
  std::array<VkPipelineShaderStageCreateInfo, 2> shader_stage_infos = {
    VkPipelineShaderStageCreateInfo {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vertex_shader,
      .pName = "main",
    },
    VkPipelineShaderStageCreateInfo {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_shader,
      .pName = "main",
    },
  };

  // 很多状态必须提前烘焙到管道中
  // 如果某些状态想要动态设置，可以用 VkPipelineDynamicStateCreateInfo 设置 多个 VkDynamicState
  std::array<VkDynamicState, 2> dynamic_states = {
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
  };

  VkPipelineDynamicStateCreateInfo dynamic_state_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = dynamic_states.size(),
    .pDynamicStates = dynamic_states.data(),
  };

  VkPipelineVertexInputStateCreateInfo vertex_input_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .vertexBindingDescriptionCount = 0,
    .pVertexBindingDescriptions = nullptr,
    .vertexAttributeDescriptionCount = 0,
    .pVertexAttributeDescriptions = nullptr,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    // VK_PRIMITIVE_TOPOLOGY_POINT_LIST
    // VK_PRIMITIVE_TOPOLOGY_LINE_LIST: 不复用的线
    // VK_PRIMITIVE_TOPOLOGY_LINE_STRIP: 首尾相连的线
    // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST: 不复用的三角形
    // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: 下一个三角形的前两条边是上一个三角形的后两条边
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    // 当_STRIP topology下，如果为True，则可以用特殊索引值来 break up 线和三角形
    .primitiveRestartEnable = VK_FALSE,
  };

  VkPipelineViewportStateCreateInfo viewport_state_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    // viewport 和 scissor 是动态的，这里设置为nullptr，等记录命令的时候动态设置
    .viewportCount = 1,
    .pViewports = nullptr,
    .scissorCount = 1,
    .pScissors = nullptr,
  };

  VkPipelineRasterizationStateCreateInfo rasterizer_state_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    // 超过深度范围的会被 clamp， 需要 gpu 支持
    .depthClampEnable = VK_FALSE,
    // 如果开启，geometry 永远不会经过光栅化阶段
    .rasterizerDiscardEnable = VK_FALSE,
    // 如何绘制多边形，除了FILL外皆需要 gpu 支持
    .polygonMode = VK_POLYGON_MODE_FILL,
    // 背面剔除
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_CLOCKWISE,
    // 深度偏移
    .depthBiasEnable = VK_FALSE,
    // 不为1.0的都需要 gpu 支持
    .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisampling_state_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
  };
  // 颜色混合：将片段着色器返回的颜色与缓冲区中的颜色进行混合
  /**
   * if (blendEnable) {
   *   finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor * oldColor.rgb);
   *   finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a);
   * } else {
   *   finalColor = newColor;
   * }
   * finalColor = finalColor & colorWriteMask;
   */
  VkPipelineColorBlendAttachmentState color_blend_attachment{
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
  };
  if constexpr (enable_blending_color) {
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor =  VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor =  VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor =  VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  } else {
    color_blend_attachment.blendEnable = VK_FALSE;
  }

  VkPipelineColorBlendStateCreateInfo color_blend_state_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    // 启用第二种混合方法
    // Combine the old and new value using a bitwise operation
    .logicOpEnable = VK_FALSE,
    .attachmentCount = 1,
    .pAttachments = &color_blend_attachment,
  };

  // 指定 uniform 全局变量
  VkPipelineLayoutCreateInfo pipeline_layout_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  };
  VkPipelineLayout pipeline_layout = createVkResource(
      vkCreatePipelineLayout, "pipeline layout", device, &pipeline_layout_info);
  VkGraphicsPipelineCreateInfo pipeline_create_info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = shader_stage_infos.size(),
      .pStages = shader_stage_infos.data(),
      .pVertexInputState = &vertex_input_info,
      .pInputAssemblyState = &input_assembly_info,
      .pTessellationState = nullptr,
      .pViewportState = &viewport_state_info,
      .pRasterizationState = &rasterizer_state_info,
      .pMultisampleState = &multisampling_state_info,
      .pDepthStencilState = nullptr,
      .pColorBlendState = &color_blend_state_info,
      .pDynamicState = &dynamic_state_info,
      .layout = pipeline_layout,
      .renderPass = render_pass,
      .subpass = 0,
      // 相同功能的管道可以共用
      .basePipelineHandle = VK_NULL_HANDLE,
  };

  VkPipeline pipeline =
      createVkResource(vkCreateGraphicsPipelines, "graphics pipeline", device,
                       VK_NULL_HANDLE, 1, &pipeline_create_info);
  return {vertex_shader, frag_shader, pipeline_layout, pipeline};
}

void destroyGraphicsPipeline(PipelineResource pipeline_resource,
                             VkDevice device) noexcept {
  vkDestroyPipeline(device, pipeline_resource.pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipeline_resource.pipeline_layout, nullptr);
  vkDestroyShaderModule(device, pipeline_resource.frag_shader, nullptr);
  vkDestroyShaderModule(device, pipeline_resource.vertex_shader, nullptr);
}

auto createFramebuffers(VkRenderPass render_pass, VkDevice device,
                        VkExtent2D extent, std::span<VkImageView> image_views)
    -> std::vector<VkFramebuffer> {
  return image_views |
         views::transform([render_pass, device, extent](auto image_view) {
           VkFramebufferCreateInfo create_info{
               .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
               .renderPass = render_pass,
               .attachmentCount = 1,
               .pAttachments = &image_view,
               .width = extent.width,
               .height = extent.height,
               .layers = 1,
           };
           return createVkResource(vkCreateFramebuffer, "framebuffer", device,
                                   &create_info);
         }) |
         ranges::to<std::vector>();
}

void destroyFramebuffers(std::span<VkFramebuffer> framebuffers,
                         VkDevice device) noexcept {
  for (auto framebuffer : framebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }
}

auto createCommandPool(VkDevice device, uint32_t graphic_family_index)
    -> VkCommandPool {
  VkCommandPoolCreateInfo pool_create_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    // VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT：允许重置单个command
    // buffer，否则就要重置命令池里的所有buffer
    // VK_COMMAND_POOL_CREATE_TRANSIENT_BIT: 命令缓冲区会很频繁的记录新命令
    .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = graphic_family_index,
  };
  return createVkResource(vkCreateCommandPool, "command pool", device,
                          &pool_create_info);
}

auto allocateCommandBuffer(VkDevice device, VkCommandPool command_pool,
                           uint32_t count) -> std::vector<VkCommandBuffer> {
  VkCommandBufferAllocateInfo cbuffer_alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = command_pool,
      // VK_COMMAND_BUFFER_LEVEL_PRIMARY: 主缓冲区，类似于main
      // VK_COMMAND_BUFFER_LEVEL_SECONDARY: 次缓冲区，可复用，类似于其他函数
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = count,
  };
  std::vector<VkCommandBuffer> command_buffers(count);
  checkVkResult(vkAllocateCommandBuffers(device, &cbuffer_alloc_info,
                                         command_buffers.data()),
                "command buffer");
  return command_buffers;
}

void destroyCommandPool(VkCommandPool command_pool, VkDevice device) noexcept {
  vkDestroyCommandPool(device, command_pool, nullptr);
}

void freeCommandBuffer(VkCommandBuffer command_buffer, VkDevice device,
                       VkCommandPool command_pool) noexcept {
  vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
}