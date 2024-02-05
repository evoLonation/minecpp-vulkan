#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <format>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <map>
#include <ranges>
#include <print>
#include <algorithm>
#include <sstream>
#include <functional>

#include "vulkan_config.h"

import tool;
import log;

namespace ranges = std::ranges;
namespace views = std::views;
using namespace tool;
using namespace log;

template <typename F, typename... Args>
  requires std::invocable<F, Args &&..., uint32_t *,
                          FuncArg<sizeof...(Args) + 1, F>>
auto getVkResource(F func, Args &&...args);

template <ranges::range RequiredRange, ranges::range AvailableRange,
          typename Mapper>
  requires RangeOf<RequiredRange, const char *> &&
           std::is_invocable_r_v<std::string_view, Mapper,
                                 ranges::range_reference_t<AvailableRange>>
void checkAvaliableSupports(RequiredRange &&required_range,
                            AvailableRange &&available_range,
                            std::string_view support_name, Mapper &&mapper);

template <typename T>
concept CustomFormatter = std::is_same_v<T, VkExtensionProperties> ||
                          std::is_same_v<T, VkLayerProperties>;

template <CustomFormatter T>
class std::formatter<T> : public std::formatter<std::string> {
public:
  template <typename FormatContext, typename... Args>
  auto format(const T &e, FormatContext &ctx) const {
    return std::formatter<std::string>::format(formatString(e), ctx);
  }

private:
  auto formatString(const T &e) const -> std::string;
};


/*
 * glfw window 相关
 */

GLFWwindow *createWindow(uint32_t width, uint32_t height,
                         std::string_view title);
void destroyWindow(GLFWwindow *p_window) noexcept;

// 抛出glfw的报错（如果有的话）
void checkGlfwError();

/*
 * instance, extension, layer相关
 * instance: vulkan最底层的对象，一切皆源于instance，存储几乎所有状态
 */
struct DebugMessengerInfo{
  VkDebugUtilsMessageSeverityFlagBitsEXT message_severity_level;
  VkDebugUtilsMessageTypeFlagsEXT message_type_flags;
};
auto createInstance(std::string_view appName) -> VkInstance;
auto createInstanceAndDebugMessenger(
    std::string_view app_name, PFN_vkDebugUtilsMessengerCallbackEXT callback,
    DebugMessengerInfo& debug_messenger_info)
    -> std::pair<VkInstance, VkDebugUtilsMessengerEXT>;

void destroyInstance(VkInstance instance) noexcept;
void destroyDebugMessenger(VkDebugUtilsMessengerEXT debug_messenger,
                           VkInstance instance) noexcept;
// 获得所有可能需要提供的 instance 级别的 extension
template <bool enableValidLayer>
auto getRequiredInstanceExtensions() -> std::vector<const char *>;
template <bool enableValidLayer>
auto getRequiredLayers() -> std::vector<const char *>;
//需要借助instance 来手动加载 debugMessenger 的create和destroy函数
auto createDebugUtilsMessengerEXT(
    VkInstance instance,
		const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger) -> VkResult;
void destroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger,
                                   const VkAllocationCallbacks *pAllocator);
template <bool enable_valid_layer>
auto createInstanceTemplate(
    std::string_view appName, PFN_vkDebugUtilsMessengerCallbackEXT callback,
    DebugMessengerInfo* p_debug_messenger_info);
VKAPI_ATTR VkBool32 VKAPI_CALL
debugHandler(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
             VkDebugUtilsMessageTypeFlagsEXT message_type,
             const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data,
             void *p_user_data);

/*
 * surface 相关
 * 借助 instance 创建 surface
 */
auto createSurface(VkInstance instance, GLFWwindow *p_window) -> VkSurfaceKHR;
void destroySurface(VkSurfaceKHR surface, VkInstance instance) noexcept;

/*
 * physical device 相关
 * 借助 instance 得到所有 physical device
 * 与 surface 结合得到 physical device 的属性，并选择一个合适的
 * features, queue family indices, extensions 用于创建 logical device
 * surface capability, format, present mode 用于创建 swap chain
 */

using QueueIndexes = std::vector<std::pair<uint32_t, uint32_t>>;
struct PhysicalDeviceInfo{
	VkPhysicalDevice device;
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceFeatures features;
	VkSurfaceCapabilitiesKHR capabilities;
	VkPresentModeKHR present_mode;
	VkSurfaceFormatKHR surface_format;
	QueueIndexes queue_indices;
};
struct QueueFamilyCheckContext {
  VkPhysicalDevice device;
  VkSurfaceKHR surface;
  size_t index;
  VkQueueFamilyProperties& properties;
};
struct DeviceCheckContext {
	VkPhysicalDeviceProperties& properties;
	VkPhysicalDeviceFeatures& features;
};
struct SurfaceCheckContext {
	VkSurfaceCapabilitiesKHR& capabilities;
	ranges::ref_view<std::vector<VkPresentModeKHR>> present_modes;
	ranges::ref_view<std::vector<VkSurfaceFormatKHR>> surface_formats;
};
struct SelectedSurfaceInfo {
	VkPresentModeKHR present_mode;
	VkSurfaceFormatKHR surface_format;
};
using SurfaceChecker = std::function<std::optional<SelectedSurfaceInfo>(
    const SurfaceCheckContext &)>;
using DeviceChecker = std::function<bool(const DeviceCheckContext &)>;
using QueueFamilyChecker = std::function<bool(const QueueFamilyCheckContext &)>;
using SurfaceChecker = std::function<std::optional<SelectedSurfaceInfo>(
    const SurfaceCheckContext &)>;

auto pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface,
                        std::span<const char *> required_extensions,
                        DeviceChecker device_checker,
                        SurfaceChecker surface_checker,
                        std::span<QueueFamilyChecker> queue_chekers)
    -> PhysicalDeviceInfo;

auto getQueueFamilyIndices(VkPhysicalDevice device, VkSurfaceKHR surface,
                           std::span<QueueFamilyChecker> queue_chekers)
    -> std::optional<QueueIndexes>;

auto checkPhysicalDeviceSupport(const DeviceCheckContext &ctx) -> bool;
auto checkSurfaceSupport(const SurfaceCheckContext &ctx)
    -> std::optional<SelectedSurfaceInfo>;
auto checkGraphicQueue(const QueueFamilyCheckContext &ctx) -> bool;
auto checkPresentQueue(const QueueFamilyCheckContext &ctx) -> bool;

/*
 * logical device 相关
 * 创建 logical device, 得到 queue
 */
auto createLogicalDevice(
    const PhysicalDeviceInfo &physical_device_info,
    std::span<const char *> required_extensions)
    -> std::pair<VkDevice, std::vector<VkQueue>>;
void destroyLogicalDevice(VkDevice device) noexcept;

auto createSwapChain(VkSurfaceKHR surface, VkDevice device,
                     VkSurfaceCapabilitiesKHR capabilities,
                     VkSurfaceFormatKHR surface_format,
                     VkPresentModeKHR present_mode, GLFWwindow *p_window,
                     std::span<uint32_t> queue_family_indices)
    -> std::pair<VkSwapchainKHR, std::vector<VkImage>>;
void destroySwapChain(VkSwapchainKHR swapchain, VkDevice device) noexcept;

/****************************         ****************************/
/****************************         ****************************/
/****************************         ****************************/
/**************************** 实现部分 ****************************/
/****************************         ****************************/
/****************************         ****************************/
/****************************         ****************************/


template <typename F, typename... Args>
  requires std::invocable<F, Args &&..., uint32_t *,
                          FuncArg<sizeof...(Args) + 1, F>>
auto getVkResource(F func, Args &&...args) {
  uint32_t count;
  func(std::forward<Args>(args)..., &count, nullptr);
  std::vector<std::remove_pointer_t<FuncArg<sizeof...(Args) + 1, F>>> resources;
  resources.resize(count);
  func(args..., &count, resources.data());
  return resources;
}

template <ranges::range RequiredRange, ranges::range AvailableRange,
          typename Mapper>
  requires RangeOf<RequiredRange, const char *> &&
           std::is_invocable_r_v<std::string_view, Mapper,
                                 ranges::range_reference_t<AvailableRange>>
void checkAvaliableSupports(RequiredRange &&required_range,
                            AvailableRange &&available_range,
                            std::string_view support_name, Mapper &&mapper) {
  debugf("the required {} {} are :\n {::}", required_range.size(),
         support_name, required_range);
	debugf("the available {} {} are :\n {::}", available_range.size(),
         support_name, available_range);
  auto unsupported_range =
      required_range |
      views::filter([&available_range, &mapper](const auto &required) {
        return ranges::none_of(
            available_range,
            [&required](auto extension) { return extension == required; },
            [&mapper](auto &extension) {
              return std::string_view{mapper(extension)};
            });
      });
  if (!unsupported_range.empty()) {
    throwf("these {} requested but not available: \n{::}", support_name,
           unsupported_range);
  }
}

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

template <>
auto std::formatter<VkExtensionProperties>::formatString(
    const VkExtensionProperties &e) const -> std::string {
  return std::format("{} (version {})", e.extensionName, e.specVersion);
}
template <>
auto std::formatter<VkLayerProperties>::formatString(
    const VkLayerProperties &e) const -> std::string {
  return std::format("{} (spec version {}, implementation version {}) : {} ",
                     e.layerName, e.specVersion, e.implementationVersion,
                     e.description);
}


GLFWwindow *createWindow(uint32_t width, uint32_t height,
                         std::string_view title) {
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
	
  if (!ranges::all_of(views::iota(0u, request_size),
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
    -> std::pair<VkSwapchainKHR, std::vector<VkImage>> {
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
  if (!diff_indices.empty()) {
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

  return {swapchain, getVkResource(vkGetSwapchainImagesKHR, device, swapchain)};
}

void destroySwapChain(VkSwapchainKHR swapchain, VkDevice device) noexcept {
  vkDestroySwapchainKHR(device, swapchain, nullptr);
}

class VulkanApplication {
public:
  VulkanApplication(uint32_t width, uint32_t height, std::string_view appName);

  ~VulkanApplication();

  VulkanApplication(const VulkanApplication &other) = delete;
  VulkanApplication(VulkanApplication &&other) noexcept = delete;
  VulkanApplication &operator=(const VulkanApplication &other) = delete;
  VulkanApplication &operator=(VulkanApplication &&other) noexcept = delete;

  [[nodiscard]] GLFWwindow *pWindow() const { return p_window_; }

private:
  GLFWwindow *p_window_;

	// 该类型本质是一个指针，后续的device也类似
	VkInstance instance_;
	// vulkan中的回调也是一种资源，需要创建
	VkDebugUtilsMessengerEXT debug_messenger_;
  DebugMessengerInfo debug_messenger_info_;

	VkSurfaceKHR surface_;

  VkDevice device_;
  std::vector<VkQueue> queues_;  

private:
/*
 * swap chain 相关
 */
	VkSwapchainKHR swapchain_;

};

VulkanApplication::VulkanApplication(uint32_t width, uint32_t height,
                                     std::string_view appName)
    : p_window_(nullptr), instance_(nullptr), debug_messenger_(nullptr),
      surface_(nullptr) {
  p_window_ = createWindow(width, height, appName);
  if constexpr (enableDebugOutput) {
    debug_messenger_info_ = {
        .message_severity_level =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
        .message_type_flags =
            VK_DEBUG_UTILS_MESSAGE_TYPE_FLAG_BITS_MAX_ENUM_EXT,
    };
    std::tie(instance_, debug_messenger_) = createInstanceAndDebugMessenger(
        appName, debugHandler, debug_messenger_info_);
  } else {
    instance_ = createInstance(appName);
  }
  surface_ = createSurface(instance_, p_window_);
	// VK_KHR_SWAPCHAIN_EXTENSION_NAME 对应的扩展用于支持交换链
  std::array required_device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  auto queue_checkers =
      std::vector<std::function<bool(const QueueFamilyCheckContext &)>>{
          checkGraphicQueue, checkPresentQueue};
  auto physical_device_info =
      pickPhysicalDevice(instance_, surface_, required_device_extensions,
                         checkPhysicalDeviceSupport, checkSurfaceSupport,
                         std::span{queue_checkers});

  auto logical_device_info =
      createLogicalDevice(physical_device_info, required_device_extensions);
  device_ = logical_device_info.first;
  queues_ = std::move(logical_device_info.second);
  std::vector<uint32_t> queue_family_indices =
      physical_device_info.queue_indices |
      views::transform([](auto a) { return a.first; }) |
      ranges::to<std::vector>();
  auto swapchain_info = createSwapChain(
      surface_, device_, physical_device_info.capabilities,
      physical_device_info.surface_format, physical_device_info.present_mode,
      p_window_, std::span(queue_family_indices));
  swapchain_ = swapchain_info.first;
}

VulkanApplication::~VulkanApplication() {
  destroySwapChain(swapchain_, device_);
  destroyLogicalDevice(device_);
	destroySurface(surface_, instance_);
  if constexpr (enableDebugOutput) {
    destroyDebugMessenger(debug_messenger_, instance_);
  }
  destroyInstance(instance_);
	destroyWindow(p_window_);
}


int main() {
  try {
    test_EnumerateAdaptor();
    test_SortedRange();
    test_ChunkBy();
    std::string applicationName = "hello, vulkan!";
    uint32_t width = 800;
    uint32_t height = 600;
    VulkanApplication application{width, height, applicationName};

    glfwSetKeyCallback(
        application.pWindow(),
        [](GLFWwindow *pWindow, int key, int scancode, int action, int mods) {
          if (action == GLFW_PRESS) {
            std::cout << "press key!" << std::endl;
          }
        });

    while (!glfwWindowShouldClose(application.pWindow())) {
      glfwPollEvents();
    }

  } catch (const std::exception &e) {

    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
