module render.vk.instance;

import "vulkan_config.h";
import "glfw_config.h";
import render.vk.tool;
import toy;
import std;

namespace rd::vk {

VKAPI_ATTR VkBool32 VKAPI_CALL debugHandler(
  VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
  VkDebugUtilsMessageTypeFlagsEXT             message_type,
  const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
  void*                                       p_user_data
) {
  auto& info = *reinterpret_cast<DebugMessengerConfig*>(p_user_data);
  /*
   * VkDebugUtilsMessageSeverityFlagBitsEXT : 严重性， VERBOSE, INFO, WARNING,
   * ERROR (可以比较，越严重越大）
   * VkDebugUtilsMessageTypeFlagsEXT : 类型，GENERAL, VALIDATION, PERFORMANCE
   * return: always VK_FALSE, VK_TRUE is reserved for use in layer development
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
  toy::debugf(
    toy::NoLocation{},
    "validation layer: ({},{}) {}",
    serverityGetter(message_severity),
    typeGetter(message_type),
    p_callback_data->pMessage
  );

  return VK_FALSE;
}

auto getDebugMessengerInfo(const DebugMessengerConfig& config
) -> VkDebugUtilsMessengerCreateInfoEXT {
  return {
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback = debugHandler,
    .pUserData = reinterpret_cast<void*>(const_cast<DebugMessengerConfig*>(&config)),
  };
}

auto createInstance(
  const std::string&                                app_name,
  std::optional<VkDebugUtilsMessengerCreateInfoEXT> debug_info,
  std::span<const char* const>                      required_extensions_
) -> rs::Instance {
  /*
   * 1. 创建appInfo
   * 2. 创建createInfo（指向appInfo）
   * 3. 调用createInstance创建instance
   */
  // 大多数 info 结构体的 sType 成员都需要显式声明
  // create 函数忽略allocator参数
  // 选择开启验证层

  auto app_info = VkApplicationInfo{
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = app_name.data(),
    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
    .engineVersion = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion = VK_API_VERSION_1_0,
  };

  std::vector<const char*> required_extensions;
  std::vector<const char*> required_layers;

  required_extensions.append_range(required_extensions_);

  if (debug_info.has_value()) {
    // VK_EXT_debug_utils 扩展用于扩展debug功能
    required_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    required_layers.push_back("VK_LAYER_KHRONOS_validation");
  }

  checkAvaliableSupports(
    required_extensions,
    getVkResources(vkEnumerateInstanceExtensionProperties, nullptr),
    [](auto& extension) { return extension.extensionName; }
  );

  checkAvaliableSupports(
    required_layers,
    getVkResources(vkEnumerateInstanceLayerProperties),
    [](auto& layer) { return layer.layerName; }
  );

  auto create_info = VkInstanceCreateInfo{
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext = debug_info.has_value() ? &debug_info.value() : nullptr,
    .pApplicationInfo = &app_info,
    .enabledLayerCount = (uint32_t)required_layers.size(),
    .ppEnabledLayerNames = required_layers.data(),
    .enabledExtensionCount = (uint32_t)required_extensions.size(),
    .ppEnabledExtensionNames = required_extensions.data(),
  };

  return { create_info };
}

InstanceTemp<true>::InstanceTemp(
  const std::string& app_name, std::span<const char* const> required_extensions
) {
  _debug_config = DebugMessengerConfig{
    .message_severity_level = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
    .message_type_flags = VK_DEBUG_UTILS_MESSAGE_TYPE_FLAG_BITS_MAX_ENUM_EXT,
  };
  _instance.setInvalid();
  _instance = createInstance(app_name, getDebugMessengerInfo(_debug_config), required_extensions);
  _debug_messenger = getDebugMessengerInfo(_debug_config);
}

InstanceTemp<false>::InstanceTemp(
  const std::string& app_name, std::span<const char* const> required_extensions
) {
  _instance.setInvalid();
  _instance = createInstance(app_name, std::nullopt, required_extensions);
}

} // namespace rd::vk
