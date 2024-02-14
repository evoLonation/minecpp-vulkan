import "vulkan_config.h";

import std;
import toy;
import vulkan;
import glm;

void recordCommandBuffer(VkCommandBuffer command_buffer,
                         VkRenderPass    render_pass,
                         VkPipeline      graphics_pipeline,
                         VkExtent2D      extent,
                         VkFramebuffer   framebuffer,
                         VkBuffer        vertex_buffer,
                         uint32_t        vertex_count) {
  VkCommandBufferBeginInfo begin_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = 0,
    .pInheritanceInfo = nullptr,
  };
  checkVkResult(vkBeginCommandBuffer(command_buffer, &begin_info),
                "begin command buffer");
  VkClearValue clearColor = { .color = {
                                .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } };
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
  // VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS: render pass 命令
  // 将会从次缓冲区执行
  vkCmdBeginRenderPass(
    command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(
    command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
  // 定义了 viewport 到缓冲区的变换
  VkViewport viewport{
    .x = 0,
    .y = 0,
    .width = (float)extent.width,
    .height = (float)extent.height,
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };
  vkCmdSetViewport(command_buffer, 0, 1, &viewport);
  // 定义了缓冲区实际存储像素的区域
  VkRect2D scissor{
    .offset = { .x = 0, .y = 0 },
    .extent = extent,
  };
  vkCmdSetScissor(command_buffer, 0, 1, &scissor);
  auto offsets = std::array<VkDeviceSize, 1>{ 0 };
  vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, offsets.data());
  vkCmdDraw(command_buffer, vertex_count, 1, 0, 0);
  vkCmdEndRenderPass(command_buffer);
  checkVkResult(vkEndCommandBuffer(command_buffer), "end command buffer");
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
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
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

  VulkanApplication(const VulkanApplication& other) = delete;
  VulkanApplication(VulkanApplication&& other) noexcept = delete;
  VulkanApplication& operator=(const VulkanApplication& other) = delete;
  VulkanApplication& operator=(VulkanApplication&& other) noexcept = delete;

  [[nodiscard]] GLFWwindow* pWindow() const { return p_window_; }

  bool recreateSwapchain();
  void drawFrame();

private:
  GLFWwindow* p_window_;

  // 该类型本质是一个指针，后续的device也类似
  VkInstance instance_;
  // vulkan中的回调也是一种资源，需要创建
  VkDebugUtilsMessengerEXT debug_messenger_;
  DebugMessengerInfo       debug_messenger_info_;

  PhysicalDeviceInfo physical_device_info_;

  VkSurfaceKHR surface_;

  VkDevice device_;
  VkQueue  graphic_queue_;
  VkQueue  present_queue_;

  VkSwapchainKHR swapchain_;
  VkExtent2D     extent_;

  std::vector<VkImageView> image_views_;

  VkRenderPass render_pass_;

  std::vector<VkFramebuffer> framebuffers_;

  PipelineResource pipeline_resource_;

  VkCommandPool command_pool_;

  struct Worker {
    VkCommandBuffer command_buffer;
    VkSemaphore     image_available_sema;
    VkSemaphore     render_finished_sema;
    VkFence         queue_batch_fence;
  };
  std::vector<Worker> workers_;

  int in_flight_index_;

  bool last_present_failed_;

  VertexData2D   vertex_data_;
  VkBuffer       vertex_buffer_;
  VkDeviceMemory vertex_buffer_memory_;
};

VulkanApplication::VulkanApplication(uint32_t         width,
                                     uint32_t         height,
                                     std::string_view appName)
  : p_window_(nullptr), instance_(nullptr), debug_messenger_(nullptr),
    surface_(nullptr), in_flight_index_(0), last_present_failed_(false) {
  p_window_ = createWindow(width, height, appName);

  if constexpr (toy::enableDebugOutput) {
    debug_messenger_info_ = {
      .message_severity_level = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
      .message_type_flags = VK_DEBUG_UTILS_MESSAGE_TYPE_FLAG_BITS_MAX_ENUM_EXT,
    };
    std::tie(instance_, debug_messenger_) =
      createInstanceAndDebugMessenger(appName, debug_messenger_info_);
  } else {
    instance_ = createInstance(appName);
  }

  surface_ = createSurface(instance_, p_window_);

  // VK_KHR_SWAPCHAIN_EXTENSION_NAME 对应的扩展用于支持交换链
  auto required_device_extensions =
    std::array{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  auto queue_checkers =
    std::vector<QueueFamilyChecker>{ checkGraphicQueue, checkPresentQueue };
  physical_device_info_ = pickPhysicalDevice(instance_,
                                             surface_,
                                             required_device_extensions,
                                             checkPhysicalDeviceSupport,
                                             checkSurfaceSupport,
                                             std::span{ queue_checkers });

  std::vector<VkQueue> queues;
  std::tie(device_, queues) =
    createLogicalDevice(physical_device_info_, required_device_extensions);
  graphic_queue_ = queues[0];
  present_queue_ = queues[1];

  std::vector<uint32_t> queue_family_indices =
    physical_device_info_.queue_indices |
    views::transform([](auto a) { return a.first; }) |
    ranges::to<std::vector>();

  std::tie(swapchain_, extent_) =
    createSwapchain(surface_,
                    device_,
                    physical_device_info_.capabilities,
                    physical_device_info_.surface_format,
                    physical_device_info_.present_mode,
                    p_window_,
                    std::span(queue_family_indices),
                    VK_NULL_HANDLE)
      .value();
  image_views_ = createImageViews(
    device_, swapchain_, physical_device_info_.surface_format.format);
  render_pass_ =
    createRenderPass(device_, physical_device_info_.surface_format.format);
  framebuffers_ =
    createFramebuffers(render_pass_, device_, extent_, image_views_);
  pipeline_resource_ = createGraphicsPipeline(device_, render_pass_);
  command_pool_ = createCommandPool(device_, queue_family_indices[0]);
  int worker_count = 2;
  workers_ = allocateCommandBuffer(device_, command_pool_, worker_count) |
             views::transform([device_ = device_](auto command_buffer) {
               return Worker{
                 .command_buffer = command_buffer,
                 .image_available_sema = createSemaphore(device_),
                 .render_finished_sema = createSemaphore(device_),
                 .queue_batch_fence = createFence(device_, true),
               };
             }) |
             ranges::to<std::vector>();
  vertex_data_ = { { { { +0.0f, -0.5f }, { 1.0f, 1.0f, 1.0f } },
                     { { +0.5f, +0.5f }, { 0.0f, 1.0f, 0.0f } },
                     { { -0.5f, +0.5f }, { 0.0f, 0.0f, 1.0f } } } };
  std::tie(vertex_buffer_, vertex_buffer_memory_) =
    createVertexBuffer(vertex_data_, device_, physical_device_info_.device);
}

VulkanApplication::~VulkanApplication() {
  vkDeviceWaitIdle(device_);
  for (auto [command_buffer, sema1, sema2, fence] : workers_) {
    destroySemaphore(sema1, device_);
    destroySemaphore(sema2, device_);
    destroyFence(fence, device_);
    freeCommandBuffer(command_buffer, device_, command_pool_);
  }
  destroyCommandPool(command_pool_, device_);
  destroyFramebuffers(framebuffers_, device_);
  destroyGraphicsPipeline(pipeline_resource_, device_);
  destroyRenderPass(render_pass_, device_);
  destroyImageViews(image_views_, device_);
  destroySwapchain(swapchain_, device_);
  destroyLogicalDevice(device_);
  destroySurface(surface_, instance_);
  if constexpr (toy::enableDebugOutput) {
    destroyDebugMessenger(debug_messenger_, instance_);
  }
  destroyInstance(instance_);
  destroyWindow(p_window_);
}

bool VulkanApplication::recreateSwapchain() {
  // todo: 换成范围更小的约束
  vkDeviceWaitIdle(device_);
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    physical_device_info_.device,
    surface_,
    &physical_device_info_.capabilities);
  auto old_swap_chain = swapchain_;
  auto queue_family_indices = physical_device_info_.queue_indices |
                              views::transform([](auto a) { return a.first; }) |
                              ranges::to<std::vector>();
  if (auto ret = createSwapchain(surface_,
                                 device_,
                                 physical_device_info_.capabilities,
                                 physical_device_info_.surface_format,
                                 physical_device_info_.present_mode,
                                 p_window_,
                                 std::span(queue_family_indices),
                                 old_swap_chain);
      ret.has_value()) {
    std::tie(swapchain_, extent_) = ret.value();
  } else if (ret.error() == SwapchainCreateError::EXTENT_ZERO) {
    return false;
  } else {
    toy::throwf("create swapchain return some error different to "
                "SwapchainCreateError::EXTENT_ZERO");
  }
  auto old_image_views = std::move(image_views_);
  auto old_framebuffers = std::move(framebuffers_);
  image_views_ = createImageViews(
    device_, swapchain_, physical_device_info_.surface_format.format);
  framebuffers_ =
    createFramebuffers(render_pass_, device_, extent_, image_views_);
  destroyFramebuffers(old_framebuffers, device_);
  destroyImageViews(old_image_views, device_);
  destroySwapchain(old_swap_chain, device_);
  return true;
}

void VulkanApplication::drawFrame() {
  auto     worker = workers_[in_flight_index_];
  uint32_t image_index;
  if (auto result = vkAcquireNextImageKHR(device_,
                                          swapchain_,
                                          std::numeric_limits<uint64_t>::max(),
                                          worker.image_available_sema,
                                          VK_NULL_HANDLE,
                                          &image_index);
      result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      last_present_failed_) {
    if (recreateSwapchain()) {
      last_present_failed_ = false;
      drawFrame();
      return;
    } else {
      toy::debugf("recreate swapchain failed, draw frame return");
      return;
    }
  } else {
    checkVkResult(result, "acquire next image");
  }

  vkWaitForFences(device_,
                  1,
                  &worker.queue_batch_fence,
                  VK_TRUE,
                  std::numeric_limits<uint64_t>::max());
  vkResetFences(device_, 1, &worker.queue_batch_fence);
  vkResetCommandBuffer(worker.command_buffer, 0);
  recordCommandBuffer(worker.command_buffer,
                      render_pass_,
                      pipeline_resource_.pipeline,
                      extent_,
                      framebuffers_[image_index],
                      vertex_buffer_,
                      vertex_data_.vertices.size());
  VkPipelineStageFlags wait_stage_mask =
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit_info{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .waitSemaphoreCount = 1,
    // 对 pWaitSemaphores 中的每个 semaphore 都定义了 semaphore wait operation
    // 触发阶段由 dst stage mask 定义
    .pWaitSemaphores = &worker.image_available_sema,
    .pWaitDstStageMask = &wait_stage_mask,
    .commandBufferCount = 1,
    .pCommandBuffers = &worker.command_buffer,
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = &worker.render_finished_sema,
  };
  checkVkResult(
    vkQueueSubmit(graphic_queue_, 1, &submit_info, worker.queue_batch_fence),
    "submit queue");
  VkPresentInfoKHR present_info{
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &worker.render_finished_sema,
    .swapchainCount = 1,
    .pSwapchains = &swapchain_,
    .pImageIndices = &image_index,
    // .pResults: 当有多个 swapchain 时检查每个的result
  };
  if (auto result = vkQueuePresentKHR(present_queue_, &present_info);
      result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    toy::debugf("the queue present return {}",
                result == VK_ERROR_OUT_OF_DATE_KHR ? "out of date error"
                                                   : "sub optimal");
    last_present_failed_ = true;
  } else {
    checkVkResult(result, "present");
  }
  in_flight_index_ = (in_flight_index_ + 1) % workers_.size();
}

int main() {
  try {
    toy::test_EnumerateAdaptor();
    toy::test_SortedRange();
    toy::test_ChunkBy();
    std::string       applicationName = "hello, vulkan!";
    uint32_t          width = 800;
    uint32_t          height = 600;
    VulkanApplication application{ width, height, applicationName };

    glfwSetKeyCallback(
      application.pWindow(),
      [](GLFWwindow* pWindow, int key, int scancode, int action, int mods) {
        if (action == GLFW_PRESS) {
          std::cout << "press key!" << std::endl;
        }
      });

    while (!glfwWindowShouldClose(application.pWindow())) {
      glfwPollEvents();
      application.drawFrame();
    }

  } catch (const std::exception& e) {

    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
