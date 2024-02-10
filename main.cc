#include "vulkan_config.h"

import std;
import tool;
import log;
import vulkan;

namespace ranges = std::ranges;
namespace views = std::views;
using namespace tool;
using namespace log;

void recordCommandBuffer(VkCommandBuffer command_buffer,
                         VkRenderPass render_pass, VkPipeline graphics_pipeline,
                         VkExtent2D extent, VkFramebuffer framebuffer) {
  VkCommandBufferBeginInfo begin_info {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = 0,
    .pInheritanceInfo = nullptr,
  };
  checkVkResult(vkBeginCommandBuffer, "begin command buffer", command_buffer, &begin_info);
  VkClearValue clearColor = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}};
  VkRenderPassBeginInfo render_pass_begin_info {
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = render_pass,
    .framebuffer = framebuffer,
    .renderArea = {
      .offset = {0, 0},
      .extent = extent,
    },
    .clearValueCount = 1,
    .pClearValues = &clearColor,
  };
  // VK_SUBPASS_CONTENTS_INLINE: render pass的command被嵌入主缓冲区
  // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS: render pass 命令 将会从次缓冲区执行
  vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command_buffer,  VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
  if constexpr (dynamic_viewport_scissor) {
    VkViewport viewport {
      .x = 0,
      .y = 0,
      .width = (float) extent.width,
      .height = (float) extent.height,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
    };
    vkCmdSetViewport(command_buffer, 0, 1, &viewport);
    VkRect2D scissor {
      .offset = {.x = 0, .y = 0},
      .extent = extent,
    };
    vkCmdSetScissor(command_buffer, 0, 1, &scissor);
  }
  vkCmdDraw(command_buffer, 3, 1, 0, 0);
  vkCmdEndRenderPass(command_buffer);
  checkVkResult(vkEndCommandBuffer, "end command buffer", command_buffer);
}

auto createSemaphore(VkDevice device) -> VkSemaphore {
  VkSemaphoreCreateInfo create_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      
  };
  return createVkResource(vkCreateSemaphore, "semaphore", device, &create_info);
}
void destroySemaphore(VkSemaphore semaphore, VkDevice device) noexcept {
  vkDestroySemaphore(device, semaphore, nullptr);
}
auto createFence(VkDevice device, bool signaled) -> VkFence {
  VkFenceCreateInfo create_info{
      .sType =  VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
  };
  if (signaled) {
    create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  }
  return createVkResource(vkCreateFence, "fence", device, &create_info);
}
void destroyFence(VkFence fence, VkDevice device) noexcept {
  vkDestroyFence(device, fence, nullptr);
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

  void drawFrame();
  void waitLastRender();

private:
  GLFWwindow *p_window_;

	// 该类型本质是一个指针，后续的device也类似
	VkInstance instance_;
	// vulkan中的回调也是一种资源，需要创建
	VkDebugUtilsMessengerEXT debug_messenger_;
  DebugMessengerInfo debug_messenger_info_;

	VkSurfaceKHR surface_;

  VkDevice device_;
  VkQueue graphic_queue_;
  VkQueue present_queue_;

	VkSwapchainKHR swapchain_;
  VkExtent2D extent_;

  std::vector<VkImageView> image_views_;

  VkRenderPass render_pass_;

  PipelineResource pipeline_resource_;

  VkCommandPool command_pool_;
  VkCommandBuffer command_buffer_;

  std::vector<VkFramebuffer> framebuffers_;

  VkSemaphore image_available_sema_;
  VkSemaphore render_finished_sema_;
  VkFence in_flight_fence_;

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
  std::vector<VkQueue> queues;
  std::tie(device_, queues) =
      createLogicalDevice(physical_device_info, required_device_extensions);
  graphic_queue_ = queues[0];
  present_queue_ = queues[1];
  std::vector<uint32_t> queue_family_indices =
      physical_device_info.queue_indices |
      views::transform([](auto a) { return a.first; }) |
      ranges::to<std::vector>();
  std::tie(swapchain_, extent_) = createSwapChain(
      surface_, device_, physical_device_info.capabilities,
      physical_device_info.surface_format, physical_device_info.present_mode,
      p_window_, std::span(queue_family_indices));
  image_views_ = createImageViews(device_, swapchain_, physical_device_info.surface_format.format);
  render_pass_ = createRenderPass(device_, physical_device_info.surface_format.format);
  pipeline_resource_ = createGraphicsPipeline(device_, render_pass_, extent_);
  std::tie(command_pool_, command_buffer_) = createCommandBuffer(device_, queue_family_indices[0]);
  framebuffers_ = createFramebuffers(render_pass_, device_, extent_, image_views_);
  image_available_sema_ = createSemaphore(device_);
  render_finished_sema_ = createSemaphore(device_);
  in_flight_fence_ = createFence(device_, true);

}

VulkanApplication::~VulkanApplication() {
  destroySemaphore(image_available_sema_, device_);
  destroySemaphore(render_finished_sema_, device_);
  destroyFence(in_flight_fence_, device_);

  destroyFramebuffers(framebuffers_, device_);
  destroyCommandBuffer(command_pool_, command_buffer_, device_);
  destroyGraphicsPipeline(pipeline_resource_, device_);
  destroyRenderPass(render_pass_, device_);
  destroyImageViews(image_views_, device_);
  destroySwapChain(swapchain_, device_);
  destroyLogicalDevice(device_);
	destroySurface(surface_, instance_);
  if constexpr (enableDebugOutput) {
    destroyDebugMessenger(debug_messenger_, instance_);
  }
  destroyInstance(instance_);
	destroyWindow(p_window_);
}

void VulkanApplication::waitLastRender() {
  vkWaitForFences(device_, 1, &in_flight_fence_, VK_TRUE, UINT64_MAX);
  vkResetFences(device_, 1, &in_flight_fence_);
}
void VulkanApplication::drawFrame() {
  waitLastRender();
  uint32_t image_index;
  vkAcquireNextImageKHR(device_, swapchain_,
                        std::numeric_limits<uint64_t>::max(),
                        image_available_sema_, VK_NULL_HANDLE, &image_index);
  vkResetCommandBuffer(command_buffer_, 0);
  recordCommandBuffer(command_buffer_, render_pass_,
                      pipeline_resource_.pipeline, extent_,
                      framebuffers_[image_index]);
  VkPipelineStageFlags wait_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit_info {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = 1,
    // 对 pWaitSemaphores 中的每个 semaphore 都定义了 semaphore wait operation
    // 触发阶段由 dst stage mask 定义
    .pWaitSemaphores = &image_available_sema_,
    .pWaitDstStageMask = &wait_stage_mask,
    .commandBufferCount = 1,
    .pCommandBuffers = &command_buffer_,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = &render_finished_sema_,
  };
  checkVkResult(vkQueueSubmit, "submit queue", graphic_queue_, 1, &submit_info,
                in_flight_fence_);
  VkPresentInfoKHR present_info {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &render_finished_sema_,
    .swapchainCount = 1,
    .pSwapchains = &swapchain_,
    .pImageIndices = &image_index,
    // .pResults: 当有多个 swapchain 时检查每个的result
  };
  checkVkResult(vkQueuePresentKHR, "present", present_queue_, &present_info);

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
      application.drawFrame();
    }
    application.waitLastRender();
    
  } catch (const std::exception &e) {

    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
