module vulkan.instance;

import "vulkan_config.h";
import vulkan.tool;

import std;
import log;

using namespace log;

template <bool enable_valid_layer>
auto createInstanceTemplate(std::string_view    appName,
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
  auto       instance_creator = [&create_info, &instance]() {
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
      .pfnUserCallback = debugHandler,
      .pUserData = p_debug_messenger_info
    };
    create_info.pNext = &debug_messenger_create_info;

    instance_creator();

    if (createDebugUtilsMessengerEXT(
          instance, &debug_messenger_create_info, nullptr, &debug_messenger) !=
        VK_SUCCESS) {
      throwf("failed to create debug messenger");
    }

    return std::pair{ instance, debug_messenger };
  } else {
    instance_creator();
    return instance;
  }
}

auto createInstance(std::string_view app_name) -> VkInstance {
  return createInstanceTemplate<false>(app_name, nullptr);
}
auto createInstanceAndDebugMessenger(std::string_view    app_name,
                                     DebugMessengerInfo& debug_messenger_info)
  -> std::pair<VkInstance, VkDebugUtilsMessengerEXT> {
  return createInstanceTemplate<true>(app_name, &debug_messenger_info);
}

void destroyInstance(VkInstance instance) noexcept {
  vkDestroyInstance(instance, nullptr);
}
void destroyDebugMessenger(VkDebugUtilsMessengerEXT debug_messenger,
                           VkInstance               instance) noexcept {
  destroyDebugUtilsMessengerEXT(instance, debug_messenger, nullptr);
}

auto createDebugUtilsMessengerEXT(
  VkInstance                                instance,
  const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
  const VkAllocationCallbacks*              pAllocator,
  VkDebugUtilsMessengerEXT*                 pDebugMessenger) -> VkResult {
  if (const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
      func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void destroyDebugUtilsMessengerEXT(VkInstance                   instance,
                                   VkDebugUtilsMessengerEXT     debugMessenger,
                                   const VkAllocationCallbacks* pAllocator) {
  if (const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
      func != nullptr) {
    func(instance, debugMessenger, pAllocator);
  }
}

template <bool enable_valid_layer>
auto getRequiredInstanceExtensions() -> std::vector<const char*> {
  // glfw 需要的扩展用于 vulkan 与窗口对接
  // VK_EXT_debug_utils 扩展用于扩展debug功能
  uint32_t   glfw_required_count;
  const auto glfw_required =
    glfwGetRequiredInstanceExtensions(&glfw_required_count);

  std::vector<const char*> required_extensions{
    glfw_required, glfw_required + glfw_required_count
  };

  if constexpr (enable_valid_layer) {
    required_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  checkAvaliableSupports(
    required_extensions,
    getVkResource(vkEnumerateInstanceExtensionProperties, nullptr),
    "extensions",
    [](auto& extension) { return extension.extensionName; });

  return required_extensions;
}

template <bool enabla_valid_layer>
auto getRequiredLayers() -> std::vector<const char*> {
  std::vector<const char*> required_layers;
  if constexpr (enabla_valid_layer) {
    required_layers.push_back("VK_LAYER_KHRONOS_validation");
  }

  checkAvaliableSupports(required_layers,
                         getVkResource(vkEnumerateInstanceLayerProperties),
                         "layers",
                         [](auto& layer) { return layer.layerName; });

  return required_layers;
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debugHandler(VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
             VkDebugUtilsMessageTypeFlagsEXT             message_type,
             const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
             void*                                       p_user_data) {
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
  debugf("validation layer: ({},{}) {}",
         serverityGetter(message_severity),
         typeGetter(message_type),
         p_callback_data->pMessage);

  return VK_FALSE;
}