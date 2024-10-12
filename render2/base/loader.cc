import "vulkan_config.h";

import toy;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(
  VkInstance                                instance,
  const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
  const VkAllocationCallbacks*              pAllocator,
  VkDebugUtilsMessengerEXT*                 pMessenger
) {
  auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
    vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")
  );
  if (func != nullptr) {
    return func(instance, pCreateInfo, pAllocator, pMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(
  VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator
) {
  auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
    vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")
  );
  if (func != nullptr) {
    func(instance, messenger, pAllocator);
  } else {
    toy::debugf("error: vkDestroyDebugUtilsMessengerEXT not found");
  }
}

VKAPI_ATTR VkResult VKAPI_CALL
vkReleaseSwapchainImagesEXT(VkDevice device, const VkReleaseSwapchainImagesInfoEXT* pReleaseInfo) {
  auto func = reinterpret_cast<PFN_vkReleaseSwapchainImagesEXT>(
    vkGetDeviceProcAddr(device, "vkReleaseSwapchainImagesEXT")
  );
  if (func != nullptr) {
    return func(device, pReleaseInfo);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}