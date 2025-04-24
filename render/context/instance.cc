module;
#include <platform.h>
module render.instance;

import render.loader;
import <vulkan_config.h>;
import render.tool;

namespace rd {

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
  auto terminate = false;
  auto serverityGetter = [&](VkDebugUtilsMessageSeverityFlagBitsEXT e) {
    switch (e) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
      return "VERBOSE";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
      return "INFO";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
      return "WARNING";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
      terminate = true;
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
    {},
    "validation layer: ({},{}) {}",
    serverityGetter(message_severity),
    typeGetter(message_type),
    p_callback_data->pMessage
  );
  if (terminate) {
    // std::terminate();
  }
  return VK_FALSE;
}

InstanceResource::InstanceResource(
  const std::string& app_name, std::span<std::string> extensions, bool enable_debug_messenger
) {
  auto debug_info = std::optional<VkDebugUtilsMessengerCreateInfoEXT>{};
  auto messenger_config = std::unique_ptr<DebugMessengerConfig>{};
  if (enable_debug_messenger) {
    messenger_config.reset(new DebugMessengerConfig{
      .message_severity_level = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
      .message_type_flags = VK_DEBUG_UTILS_MESSAGE_TYPE_FLAG_BITS_MAX_ENUM_EXT,
    });
    debug_info = VkDebugUtilsMessengerCreateInfoEXT{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debugHandler,
      .pUserData = reinterpret_cast<void*>(messenger_config.get()),
    };
  }

  auto api_version = PLATFORM_VULKAN_VERSION;
  auto app_info = VkApplicationInfo{
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = app_name.data(),
    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
    .engineVersion = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion = api_version,
  };

  std::vector<const char*> required_extensions;
  std::vector<const char*> required_layers;

  required_extensions.append_range(extensions | views::transform([](const auto& str) {
                                     return str.data();
                                   }));
  if (platform::portability_subset) {
    // 此扩展允许应用程序控制是否将公开 VK_KHR_portability_subset 扩展的设备包含在物理设备枚举的结果中。
    // 由于支持 VK_KHR_portability_subset 扩展的设备并非完全符合 Vulkan 规范的实现，因此 Vulkan 加载程序不会报告这些设备，除非应用程序明确请求它们。
    // 这可以防止可能不了解非符合设备情况的应用程序意外使用它们，因为任何支持 VK_KHR_portability_subset 扩展的设备都要求在使用该设备时必须启用此扩展。
    required_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
  }

  if constexpr (toy::enable_debug) {
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
    .enabledLayerCount = (uint32)required_layers.size(),
    .ppEnabledLayerNames = required_layers.data(),
    .enabledExtensionCount = (uint32)required_extensions.size(),
    .ppEnabledExtensionNames = required_extensions.data(),
  };
  if (platform::portability_subset) {
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }

  this->instance.detachSingleton();
  auto instance = rs::Instance{ create_info };
  loadInstanceFunctions(instance);
  auto debug_messenger = rs::DebugMessenger{};
  if (enable_debug_messenger) {
    debug_messenger = { debug_info.value() };
  }
  this->instance = std::move(instance);
  this->debug_messenger = std::move(debug_messenger);
  this->messenger_config = std::move(messenger_config);
  this->api_version = api_version;
}

} // namespace rd