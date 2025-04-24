import <vulkan_config.h>;

import toy;
import render.loader;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(
  VkInstance                                instance,
  const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
  const VkAllocationCallbacks*              pAllocator,
  VkDebugUtilsMessengerEXT*                 pMessenger
) {
  return rd::p_PFN_vkCreateDebugUtilsMessengerEXT(instance, pCreateInfo, pAllocator, pMessenger);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(
  VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator
) {
  rd::p_PFN_vkDestroyDebugUtilsMessengerEXT(instance, messenger, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkReleaseSwapchainImagesEXT(VkDevice device, const VkReleaseSwapchainImagesInfoEXT* pReleaseInfo) {
  return rd::p_PFN_vkReleaseSwapchainImagesEXT(device, pReleaseInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2KHR(
  VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* pSubmits, VkFence fence
) {
  return rd::p_PFN_vkQueueSubmit2KHR(queue, submitCount, pSubmits, fence);
}

VKAPI_ATTR void VKAPI_CALL
vkCmdPipelineBarrier2KHR(VkCommandBuffer commandBuffer, const VkDependencyInfo* pDependencyInfo) {
  rd::p_PFN_vkCmdPipelineBarrier2KHR(commandBuffer, pDependencyInfo);
}