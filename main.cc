import "vulkan_config.h";

import std;
import toy;
import vulkan;
import glm;

using namespace vk;

void recordCommandBuffer(VkCommandBuffer                  command_buffer,
                         VkRenderPass                     render_pass,
                         VkPipeline                       graphics_pipeline,
                         VkExtent2D                       extent,
                         VkFramebuffer                    framebuffer,
                         VkBuffer                         vertex_buffer,
                         VkBuffer                         index_buffer,
                         uint32_t                         index_count,
                         VkPipelineLayout                 pipeline_layout,
                         std::span<const VkDescriptorSet> descriptor_sets) {
  VkCommandBufferBeginInfo begin_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    /** \param VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT specifies that each
     * recording of the command buffer will only be submitted once, and the
     * command buffer will be reset and recorded again between each submission.
     * \param VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT specifies that a
     * secondary command buffer is considered to be entirely inside a render
     * pass. If this is a primary command buffer, then this bit is ignored.
     * \param VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT specifies that a
     * command buffer can be resubmitted to any queue of the same queue family
     * while it is in the pending state, and recorded into multiple primary
     * command buffers.*/
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
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
  vkCmdBindIndexBuffer(command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16);
  vkCmdBindDescriptorSets(command_buffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_layout,
                          // firstSet: 对应着色器中的layout(set=0)
                          0,
                          descriptor_sets.size(),
                          descriptor_sets.data(),
                          0,
                          nullptr);
  vkCmdDrawIndexed(command_buffer, index_count, 1, 0, 0, 0);
  vkCmdEndRenderPass(command_buffer);
  checkVkResult(vkEndCommandBuffer(command_buffer), "end command buffer");
}

auto createSemaphore(VkDevice device) -> VkSemaphore {
  VkSemaphoreCreateInfo create_info{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,

  };
  return createVkResource(vkCreateSemaphore, device, &create_info);
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
  return createVkResource(vkCreateFence, device, &create_info);
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

  [[nodiscard]] GLFWwindow* pWindow() const { return window_.get(); }

  auto recreateSwapchain() -> bool;
  void drawFrame();

private:
  Window window_;

  template <bool enable_debug_messenger>
  struct InstanceWithDebugMessenger;

  template <>
  struct InstanceWithDebugMessenger<true> {
    Instance             instance;
    DebugMessenger       debug_messenger;
    DebugMessengerConfig debug_config;
  };
  template <>
  struct InstanceWithDebugMessenger<false> {
    Instance instance;
  };
  InstanceWithDebugMessenger<toy::enable_debug> instance_;

  PhysicalDeviceInfo physical_device_info_;

  Surface surface_;

  VkDevice device_;
  VkQueue  graphic_queue_;
  VkQueue  present_queue_;

  Swapchain                swapchain_;
  VkExtent2D               extent_;
  std::vector<ImageView>   image_views_;
  std::vector<VkImageView> image_view_handles_;

  VkRenderPass render_pass_;

  std::vector<VkFramebuffer> framebuffers_;

  VkDescriptorSetLayout descriptor_set_layout_;
  PipelineResource      pipeline_resource_;

  VkCommandPool graphics_command_pool_;

  struct Worker {
    VkCommandBuffer command_buffer;
    VkSemaphore     image_available_sema;
    VkSemaphore     render_finished_sema;
    VkFence         queue_batch_fence;
    VkBuffer        uniform_buffer;
    VkDeviceMemory  uniform_memory;
    void*           uniform_memory_map;
    VkDescriptorSet descriptor_set;
  };
  std::vector<Worker> workers_;

  void updateUniformBuffer(Worker worker);

  int in_flight_index_;

  bool last_present_failed_;

  using Vertex2D = Vertex<glm::vec2, glm::vec3>;
  std::vector<Vertex2D> vertex_data_;
  VkQueue               transfer_queue_;
  VkCommandPool         transfer_command_pool_;
  VkCommandBuffer       transfer_command_buffer_;
  VkFence               transfer_fence_;
  VkBuffer              vertex_buffer_;
  VkDeviceMemory        vertex_buffer_memory_;
  std::vector<uint16_t> vertex_indices;
  VkBuffer              index_buffer_;
  VkDeviceMemory        index_buffer_memory_;

  struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
  };
  // std::vector<std::pair<VkBuffer, VkDeviceMemory>> uniform_buffers_;
  // std::vector<void*>                               uniform_buffer_maps_;

  VkDescriptorPool descriptor_pool_;
  // std::vector<VkDescriptorSet> descriptor_sets_;
};

VulkanApplication::VulkanApplication(uint32_t         width,
                                     uint32_t         height,
                                     std::string_view app_name)
  : window_(), instance_(), surface_(), in_flight_index_(0),
    last_present_failed_(false) {
  window_ = Window(width, height, app_name);

  if constexpr (toy::enable_debug) {
    instance_.debug_config = DebugMessengerConfig{
      .message_severity_level = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
      .message_type_flags = VK_DEBUG_UTILS_MESSAGE_TYPE_FLAG_BITS_MAX_ENUM_EXT,
    };
    instance_.instance = createInstance(app_name, instance_.debug_config);
    instance_.debug_messenger =
      createDebugMessenger(instance_.instance.get(), instance_.debug_config);
  } else {
    instance_.instance = createInstance(app_name);
  }

  surface_ = createSurface(instance_.instance.get(), window_.get());

  // VK_KHR_SWAPCHAIN_EXTENSION_NAME 对应的扩展用于支持交换链
  auto required_device_extensions =
    std::array{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  physical_device_info_ = pickPhysicalDevice(
    instance_.instance.get(),
    surface_.get(),
    required_device_extensions,
    checkPhysicalDeviceSupport,
    checkSurfaceSupport,
    std::array<QueueFamilyChecker, 3>{
      checkGraphicQueue, checkPresentQueue, checkTransferQueue });
  toy::debug(physical_device_info_.queue_indices);
  toy::debugf("graphic family index {} transfer family index",
              physical_device_info_.queue_indices[0].first ==
                  physical_device_info_.queue_indices[2].first
                ? "=="
                : "!=");

  std::vector<VkQueue> queues;
  std::tie(device_, queues) =
    createLogicalDevice(physical_device_info_, required_device_extensions);
  graphic_queue_ = queues[0];
  present_queue_ = queues[1];

  auto image_sharing_families = std::vector<uint32_t>{
    physical_device_info_.queue_indices[0].first,
    physical_device_info_.queue_indices[1].first,
  };

  std::tie(swapchain_, extent_) =
    createSwapchain(surface_.get(),
                    device_,
                    physical_device_info_.capabilities,
                    physical_device_info_.surface_format,
                    physical_device_info_.present_mode,
                    window_.get(),
                    std::span(image_sharing_families),
                    VK_NULL_HANDLE)
      .value();
  image_views_ = createImageViews(
    device_, swapchain_.get(), physical_device_info_.surface_format.format);
  image_view_handles_ =
    views::transform(image_views_,
                     [](const auto& image_view) { return image_view.get(); }) |
    ranges::to<std::vector>();
  render_pass_ =
    createRenderPass(device_, physical_device_info_.surface_format.format);
  framebuffers_ =
    createFramebuffers(render_pass_, device_, extent_, image_view_handles_);
  descriptor_set_layout_ = createDescriptorSetLayout(device_);
  auto vertex_info = Vertex2D::getVertexInfo();
  pipeline_resource_ =
    createGraphicsPipeline(device_,
                           render_pass_,
                           std::span{ &vertex_info.binding_description, 1 },
                           vertex_info.attribute_descriptions,
                           std::span{ &descriptor_set_layout_, 1 });
  graphics_command_pool_ =
    createCommandPool(device_, physical_device_info_.queue_indices[0].first);
  int worker_count = 2;
  descriptor_pool_ = createDescriptorPool(worker_count, device_);
  workers_ =
    views::zip(
      allocateCommandBuffers(device_, graphics_command_pool_, worker_count),
      allocateDescriptorSets(views::repeat(descriptor_set_layout_) |
                               views::take(worker_count) |
                               ranges::to<std::vector>(),
                             device_,
                             descriptor_pool_)) |
    views::transform(
      [device = device_,
       descriptor_set_layout = descriptor_set_layout_,
       physical_device = physical_device_info_.device](auto pair) {
        auto [command_buffer, descriptor_set] = pair;
        auto [uniform_buffer, uniform_memory] =
          createBuffer(device,
                       physical_device,
                       sizeof(UniformBufferObject),
                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* map;
        vkMapMemory(
          device, uniform_memory, 0, sizeof(UniformBufferObject), 0, &map);
        return Worker{
          .command_buffer = command_buffer,
          .image_available_sema = createSemaphore(device),
          .render_finished_sema = createSemaphore(device),
          .queue_batch_fence = createFence(device, true),
          .uniform_buffer = uniform_buffer,
          .uniform_memory = uniform_memory,
          .uniform_memory_map = map,
          .descriptor_set = descriptor_set,
        };
      }) |
    ranges::to<std::vector>();
  vertex_data_ = {
    { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f } },
    { { +0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
    { { +0.5f, +0.5f }, { 0.0f, 0.0f, 1.0f } },
    { { -0.5f, +0.5f }, { 1.0f, 1.0f, 1.0f } },
  };
  vertex_indices = {
    0, 1, 2, 2, 3, 0,
  };
  transfer_command_pool_ =
    createCommandPool(device_, physical_device_info_.queue_indices[2].first);
  transfer_queue_ = queues[2];
  transfer_command_buffer_ =
    allocateCommandBuffers(device_, transfer_command_pool_, 1)[0];
  transfer_fence_ = createFence(device_, false);
  std::tie(vertex_buffer_, vertex_buffer_memory_) =
    createVertexBuffer(vertex_data_,
                       physical_device_info_.device,
                       device_,
                       transfer_queue_,
                       transfer_command_buffer_,
                       transfer_fence_);
  std::tie(index_buffer_, index_buffer_memory_) =
    createIndexBuffer(vertex_indices,
                      physical_device_info_.device,
                      device_,
                      transfer_queue_,
                      transfer_command_buffer_,
                      transfer_fence_);
  // uniform_buffers_.resize(worker_count);
  // uniform_buffer_maps_.resize(worker_count);
  // for (int i = 0; i < worker_count; i++) {
  //   uniform_buffers_[i] = createBuffer(device_,
  //                                      physical_device_info_.device,
  //                                      sizeof(UniformBufferObject),
  //                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
  //                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
  //                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  //   vkMapMemory(device_,
  //               uniform_buffers_[i].second,
  //               0,
  //               sizeof(UniformBufferObject),
  //               0,
  //               &uniform_buffer_maps_[i]);
  // }

  // descriptor_sets_ = allocateDescriptorSets(
  //   views::repeat(descriptor_set_layout_) | views::take(worker_count) |
  //     ranges::to<std::vector>(),
  //   device_,
  //   descriptor_pool_);
}

VulkanApplication::~VulkanApplication() {
  // for (auto [buffer, memory] : uniform_buffers_) {
  //   vkUnmapMemory(device_, memory);
  //   destroyBuffer(buffer, memory, device_);
  // }
  vkDeviceWaitIdle(device_);
  destroyBuffer(index_buffer_, index_buffer_memory_, device_);
  destroyBuffer(vertex_buffer_, vertex_buffer_memory_, device_);
  destroyFence(transfer_fence_, device_);
  freeCommandBuffer(transfer_command_buffer_, device_, transfer_command_pool_);
  destroyCommandPool(transfer_command_pool_, device_);

  freeDescriptorSets(workers_ | views::transform([](auto worker) {
                       return worker.descriptor_set;
                     }) |
                       ranges::to<std::vector>(),
                     device_,
                     descriptor_pool_);
  for (auto [command_buffer, sema1, sema2, fence, buffer, memory, _1, _2] :
       workers_) {
    vkUnmapMemory(device_, memory);
    destroyBuffer(buffer, memory, device_);
    destroySemaphore(sema1, device_);
    destroySemaphore(sema2, device_);
    destroyFence(fence, device_);
    freeCommandBuffer(command_buffer, device_, graphics_command_pool_);
  }
  destroyDescriptorPool(descriptor_pool_, device_);
  destroyCommandPool(graphics_command_pool_, device_);
  destroyFramebuffers(framebuffers_, device_);
  destroyGraphicsPipeline(pipeline_resource_, device_);
  destroyDescriptorSetLayout(descriptor_set_layout_, device_);
  destroyRenderPass(render_pass_, device_);
  destroyLogicalDevice(device_);
}

auto VulkanApplication::recreateSwapchain() -> bool {
  // todo: 换成范围更小的约束
  vkDeviceWaitIdle(device_);
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    physical_device_info_.device,
    surface_.get(),
    &physical_device_info_.capabilities);
  auto old_swap_chain = std::move(swapchain_);
  auto old_image_views = std::move(image_views_);
  auto old_framebuffers = std::move(framebuffers_);
  auto queue_family_indices = physical_device_info_.queue_indices |
                              views::transform([](auto a) { return a.first; }) |
                              ranges::to<std::vector>();
  if (auto ret = createSwapchain(surface_.get(),
                                 device_,
                                 physical_device_info_.capabilities,
                                 physical_device_info_.surface_format,
                                 physical_device_info_.present_mode,
                                 window_.get(),
                                 std::span(queue_family_indices),
                                 old_swap_chain.get());
      ret.has_value()) {
    std::tie(swapchain_, extent_) = std::move(ret.value());
  } else if (ret.error() == SwapchainCreateError::EXTENT_ZERO) {
    return false;
  } else {
    toy::throwf("create swapchain return some error different to "
                "SwapchainCreateError::EXTENT_ZERO");
  }
  image_views_ = createImageViews(
    device_, swapchain_.get(), physical_device_info_.surface_format.format);
  image_view_handles_ =
    views::transform(image_views_,
                     [](const auto& image_view) { return image_view.get(); }) |
    ranges::to<std::vector>();
  framebuffers_ =
    createFramebuffers(render_pass_, device_, extent_, image_view_handles_);
  destroyFramebuffers(old_framebuffers, device_);
  return true;
}

void VulkanApplication::updateUniformBuffer(Worker worker) {
  static auto startTime = std::chrono::high_resolution_clock::now();

  auto  currentTime = std::chrono::high_resolution_clock::now();
  float time = std::chrono::duration<float, std::chrono::seconds::period>(
                 currentTime - startTime)
                 .count();
  auto ubo = UniformBufferObject{
    .model = glm::rotate(
      glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
    .view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f),
                        glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.0f, 0.0f, 1.0f)),
    .proj = glm::perspective(
      glm::radians(45.0f), extent_.width * 1.0f / extent_.height, 0.1f, 10.0f),
  };
  ubo.proj[1][1] *= -1;
  std::memcpy(
    worker.uniform_memory_map, reinterpret_cast<void*>(&ubo), sizeof(ubo));
  updateDescriptorSet(worker.descriptor_set, worker.uniform_buffer, device_);
}

void VulkanApplication::drawFrame() {
  auto     worker = workers_[in_flight_index_];
  uint32_t image_index;
  if (auto result = vkAcquireNextImageKHR(device_,
                                          swapchain_.get(),
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
  updateUniformBuffer(worker);
  vkResetCommandBuffer(worker.command_buffer, 0);
  recordCommandBuffer(worker.command_buffer,
                      render_pass_,
                      pipeline_resource_.pipeline,
                      extent_,
                      framebuffers_[image_index],
                      vertex_buffer_,
                      index_buffer_,
                      vertex_indices.size(),
                      pipeline_resource_.pipeline_layout,
                      std::span{ &worker.descriptor_set, 1 });
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
    .pSwapchains = &swapchain_.get(),
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
