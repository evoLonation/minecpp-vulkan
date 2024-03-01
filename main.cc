import "vulkan_config.h";

import std;
import toy;
import vulkan;
import glm;

using namespace vk;

void recordCommandBuffer(
  VkCommandBuffer                  command_buffer,
  VkRenderPass                     render_pass,
  VkPipeline                       graphics_pipeline,
  VkExtent2D                       extent,
  VkFramebuffer                    framebuffer,
  VkBuffer                         vertex_buffer,
  VkBuffer                         index_buffer,
  uint32_t                         index_count,
  VkPipelineLayout                 pipeline_layout,
  std::span<const VkDescriptorSet> descriptor_sets
) {
  VkClearValue clearColor = { .color = { .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } } };
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
  vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline);
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
  vkCmdBindDescriptorSets(
    command_buffer,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    pipeline_layout,
    // firstSet: 对应着色器中的layout(set=0)
    0,
    descriptor_sets.size(),
    descriptor_sets.data(),
    0,
    nullptr
  );
  vkCmdDrawIndexed(command_buffer, index_count, 1, 0, 0, 0);
  vkCmdEndRenderPass(command_buffer);
}

class VulkanApplication {
public:
  VulkanApplication(uint32_t width, uint32_t height, std::string_view appName);

  ~VulkanApplication();

  VulkanApplication(const VulkanApplication& other) = delete;
  VulkanApplication(VulkanApplication&& other) noexcept = delete;
  VulkanApplication& operator=(const VulkanApplication& other) = delete;
  VulkanApplication& operator=(VulkanApplication&& other) noexcept = delete;

  [[nodiscard]] GLFWwindow* pWindow() const { return window_; }

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

  Device  device_;
  VkQueue graphic_queue_;
  VkQueue present_queue_;

  Swapchain              swapchain_;
  VkExtent2D             extent_;
  std::vector<ImageView> image_views_;

  RenderPass render_pass_;

  std::vector<Framebuffer> framebuffers_;

  DescriptorSetLayout uniform_set_layout_;
  DescriptorSetLayout sampler_set_layout_;
  PipelineResource    pipeline_resource_;

  CommandPool    graphics_command_pool_;
  DescriptorPool descriptor_pool_;

  CommandBuffers graphics_command_buffers_;
  DescriptorSets descriptor_sets_;

  VkDescriptorSet sampler_set_;
  struct Worker {
    VkCommandBuffer command_buffer;
    Semaphore       image_available_sema;
    Semaphore       render_finished_sema;
    Fence           queue_batch_fence;
    Buffer          uniform_buffer;
    Memory          uniform_memory;
    void*           uniform_memory_map;
    VkDescriptorSet uniform_set;
  };
  std::vector<Worker> workers_;

  void updateUniformBuffer(const Worker& worker);

  int in_flight_index_;

  bool last_present_failed_;

  using Vertex2D = Vertex<glm::vec2, glm::vec3, glm::vec2>;
  std::vector<Vertex2D> vertex_data_;
  VkQueue               transfer_queue_;
  CommandPool           transfer_command_pool_;
  CommandBuffers        transfer_command_buffer_container_;
  VkCommandBuffer       transfer_command_buffer_;
  Fence                 transfer_fence_;
  Buffer                vertex_buffer_;
  Memory                vertex_buffer_memory_;
  std::vector<uint16_t> vertex_indices;
  Buffer                index_buffer_;
  Memory                index_buffer_memory_;

  struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
  };

  Image     texture_image_;
  ImageView texture_image_view_;
  Memory    texture_memory_;
  Sampler   texture_sampler_;
};

VulkanApplication::VulkanApplication(uint32_t width, uint32_t height, std::string_view app_name)
  : in_flight_index_(0), last_present_failed_(false) {
  window_ = Window{ width, height, app_name };

  if constexpr (toy::enable_debug) {
    instance_.debug_config = DebugMessengerConfig{
      .message_severity_level = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
      .message_type_flags = VK_DEBUG_UTILS_MESSAGE_TYPE_FLAG_BITS_MAX_ENUM_EXT,
    };
    instance_.instance = createInstance(app_name, instance_.debug_config);
    instance_.debug_messenger = createDebugMessenger(instance_.instance, instance_.debug_config);
  } else {
    instance_.instance = createInstance(app_name);
  }

  surface_ = createSurface(instance_.instance, window_);

  // VK_KHR_SWAPCHAIN_EXTENSION_NAME 对应的扩展用于支持交换链
  auto required_device_extensions = std::array{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  physical_device_info_ = pickPhysicalDevice(
    instance_.instance,
    surface_,
    required_device_extensions,
    checkPhysicalDeviceSupport,
    checkSurfaceSupport,
    std::array<QueueFamilyChecker, 3>{ checkGraphicQueue, checkPresentQueue, checkTransferQueue }
  );
  toy::debug(physical_device_info_.queue_indices);
  toy::debugf(
    "graphic family index {} transfer family index",
    physical_device_info_.queue_indices[0].first == physical_device_info_.queue_indices[2].first
      ? "=="
      : "!="
  );

  std::vector<VkQueue> queues;
  std::tie(device_, queues) = createDevice(physical_device_info_, required_device_extensions);
  graphic_queue_ = queues[0];
  present_queue_ = queues[1];

  auto image_sharing_families = std::vector<uint32_t>{
    physical_device_info_.queue_indices[0].first,
    physical_device_info_.queue_indices[1].first,
  };

  std::tie(swapchain_, extent_) = createSwapchain(
                                    surface_,
                                    device_,
                                    physical_device_info_.capabilities,
                                    physical_device_info_.surface_format,
                                    physical_device_info_.present_mode,
                                    window_,
                                    std::span(image_sharing_families),
                                    VK_NULL_HANDLE
  )
                                    .value();
  image_views_ =
    createSwapchainImageViews(device_, swapchain_, physical_device_info_.surface_format.format);
  render_pass_ = createRenderPass(device_, physical_device_info_.surface_format.format);
  framebuffers_ = createFramebuffers(
    render_pass_,
    device_,
    extent_,
    views::transform(image_views_, [](const auto& image_view) { return image_view.get(); }) |
      ranges::to<std::vector>()
  );
  uniform_set_layout_ = createUniformDescriptorSetLayout(device_);
  sampler_set_layout_ = createSamplerDescriptorSetLayout(device_);

  auto vertex_info = Vertex2D::getVertexInfo();
  pipeline_resource_ = createGraphicsPipeline(
    device_,
    render_pass_,
    std::array{ vertex_info.binding_description },
    vertex_info.attribute_descriptions,
    std::array{ uniform_set_layout_.get(), sampler_set_layout_.get() }
  );
  graphics_command_pool_ = createCommandPool(device_, physical_device_info_.queue_indices[0].first);
  int  worker_count = 2;
  auto descriptor_type_counts = std::array{ VkDescriptorPoolSize{
                                              .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                              .descriptorCount = (uint32_t)worker_count,
                                            },
                                            VkDescriptorPoolSize{
                                              .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                              .descriptorCount = 1,
                                            } };
  descriptor_pool_ = createDescriptorPool(device_, worker_count + 1, descriptor_type_counts);
  graphics_command_buffers_ = allocateCommandBuffers(device_, graphics_command_pool_, worker_count);
  auto uniform_set_layouts =
    views::repeat(uniform_set_layout_.get(), worker_count) | ranges::to<std::vector>();

  auto descriptor_set_layouts =
    views::join(std::array{ views::repeat(sampler_set_layout_.get(), 1),
                            views::repeat(uniform_set_layout_.get(), worker_count) }) |
    ranges::to<std::vector>();
  descriptor_sets_ = allocateDescriptorSets(device_, descriptor_pool_, descriptor_set_layouts);
  sampler_set_ = descriptor_sets_.get()[0];
  auto uniform_sets = descriptor_sets_.get() | views::drop(1);
  workers_ = views::zip(graphics_command_buffers_.get(), uniform_sets) |
             views::transform([device = device_.get(),
                               descriptor_set_layout = uniform_set_layout_.get(),
                               physical_device = physical_device_info_.device](auto pair) {
               auto [command_buffer, descriptor_set] = pair;
               auto [uniform_buffer, uniform_memory] = createBuffer(
                 physical_device,
                 device,
                 sizeof(UniformBufferObject),
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
               );
               void* map;
               vkMapMemory(device, uniform_memory, 0, sizeof(UniformBufferObject), 0, &map);
               return Worker{
                 .command_buffer = command_buffer,
                 .image_available_sema = createSemaphore(device),
                 .render_finished_sema = createSemaphore(device),
                 .queue_batch_fence = createFence(device, true),
                 .uniform_buffer = std::move(uniform_buffer),
                 .uniform_memory = std::move(uniform_memory),
                 .uniform_memory_map = map,
                 .uniform_set = descriptor_set,
               };
             }) |
             ranges::to<std::vector>();
  vertex_data_ = {
    { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
    { { +0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
    { { +0.5f, +0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
    { { -0.5f, +0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
  };
  vertex_indices = {
    0, 1, 2, 2, 3, 0,
  };
  transfer_command_pool_ = createCommandPool(device_, physical_device_info_.queue_indices[2].first);
  transfer_queue_ = queues[2];
  transfer_command_buffer_container_ = allocateCommandBuffers(device_, transfer_command_pool_, 1);
  transfer_command_buffer_ = transfer_command_buffer_container_.get()[0];
  transfer_fence_ = createFence(device_, false);
  std::tie(vertex_buffer_, vertex_buffer_memory_) = createVertexBuffer(
    physical_device_info_.device,
    device_,
    transfer_queue_,
    transfer_command_buffer_,
    vertex_data_,
    transfer_fence_
  );
  std::tie(index_buffer_, index_buffer_memory_) = createIndexBuffer(
    physical_device_info_.device,
    device_,
    transfer_queue_,
    transfer_command_buffer_,
    vertex_indices,
    transfer_fence_
  );
  auto max_anisotropy = physical_device_info_.properties.limits.maxSamplerAnisotropy;
  std::tie(this->texture_image_, this->texture_memory_, this->texture_image_view_) =
    createTextureImage(
      physical_device_info_.device,
      device_,
      graphic_queue_,
      transfer_command_buffer_,
      transfer_fence_
    );
  this->texture_sampler_ = createSampler(device_, max_anisotropy);
  updateDescriptorSet(
    device_,
    sampler_set_,
    std::pair{ this->texture_image_view_.get(), this->texture_sampler_.get() }
  );
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
  vkDeviceWaitIdle(device_);
  for (auto& worker : workers_) {
    vkUnmapMemory(device_, worker.uniform_memory);
  }
}

auto VulkanApplication::recreateSwapchain() -> bool {
  // todo: 换成范围更小的约束
  vkDeviceWaitIdle(device_);
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    physical_device_info_.device, surface_, &physical_device_info_.capabilities
  );
  auto old_swap_chain = std::move(swapchain_);
  auto old_image_views = std::move(image_views_);
  auto old_framebuffers = std::move(framebuffers_);
  auto queue_family_indices = physical_device_info_.queue_indices |
                              views::transform([](auto a) { return a.first; }) |
                              ranges::to<std::vector>();
  if (auto ret = createSwapchain(
        surface_,
        device_,
        physical_device_info_.capabilities,
        physical_device_info_.surface_format,
        physical_device_info_.present_mode,
        window_,
        std::span(queue_family_indices),
        old_swap_chain
      );
      ret.has_value()) {
    std::tie(swapchain_, extent_) = std::move(ret.value());
  } else if (ret.error() == SwapchainCreateError::EXTENT_ZERO) {
    return false;
  } else {
    toy::throwf("create swapchain return some error different to "
                "SwapchainCreateError::EXTENT_ZERO");
  }
  image_views_ =
    createSwapchainImageViews(device_, swapchain_, physical_device_info_.surface_format.format);
  framebuffers_ = createFramebuffers(
    render_pass_,
    device_,
    extent_,
    views::transform(image_views_, [](const auto& image_view) { return image_view.get(); }) |
      ranges::to<std::vector>()
  );
  return true;
}

void VulkanApplication::updateUniformBuffer(const Worker& worker) {
  static auto startTime = std::chrono::high_resolution_clock::now();

  auto  currentTime = std::chrono::high_resolution_clock::now();
  float time =
    std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
  auto ubo = UniformBufferObject{
    .model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
    .view = glm::lookAt(
      glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)
    ),
    .proj =
      glm::perspective(glm::radians(45.0f), extent_.width * 1.0f / extent_.height, 0.1f, 10.0f),
  };
  ubo.proj[1][1] *= -1;
  std::memcpy(worker.uniform_memory_map, reinterpret_cast<void*>(&ubo), sizeof(ubo));
  updateDescriptorSet(device_, worker.uniform_set, worker.uniform_buffer);
}

void VulkanApplication::drawFrame() {
  auto&    worker = workers_[in_flight_index_];
  uint32_t image_index;

  auto wait_fence = worker.queue_batch_fence.get();
  vkWaitForFences(device_, 1, &wait_fence, VK_TRUE, std::numeric_limits<uint64_t>::max());

  if (auto result = vkAcquireNextImageKHR(
        device_,
        swapchain_,
        std::numeric_limits<uint64_t>::max(),
        worker.image_available_sema,
        VK_NULL_HANDLE,
        &image_index
      );
      result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || last_present_failed_) {
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
  // why vkResetFences after acquire image?
  // because acquire image maybe call drawFrame() recursively, if vkResetFences
  // befor acquire image, vkResetFences maybe call twice with only one signal
  // operation by vkQueueSubmit
  vkResetFences(device_, 1, &wait_fence);
  updateUniformBuffer(worker);
  // vkBeginCommandBuffer 会隐式执行vkResetCommandBuffer
  // vkResetCommandBuffer(worker.command_buffer, 0);
  recordAndSubmit(
    worker.command_buffer,
    graphic_queue_,
    [&]() {
      recordCommandBuffer(
        worker.command_buffer,
        render_pass_,
        pipeline_resource_.pipeline,
        extent_,
        framebuffers_[image_index],
        vertex_buffer_,
        index_buffer_,
        vertex_indices.size(),
        pipeline_resource_.pipeline_layout,
        std::array{ worker.uniform_set, sampler_set_ }
      );
    },
    std::array<std::pair<VkSemaphore, VkPipelineStageFlags>, 1>{ std::pair{
      worker.image_available_sema.get(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT } },
    std::array{ worker.render_finished_sema.get() },
    worker.queue_batch_fence
  );

  auto present_info = VkPresentInfoKHR{
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &worker.render_finished_sema.get(),
    .swapchainCount = 1,
    .pSwapchains = &swapchain_.get(),
    .pImageIndices = &image_index,
    // .pResults: 当有多个 swapchain 时检查每个的result
  };
  if (auto result = vkQueuePresentKHR(present_queue_, &present_info);
      result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    toy::debugf(
      "the queue present return {}",
      result == VK_ERROR_OUT_OF_DATE_KHR ? "out of date error" : "sub optimal"
    );
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
      }
    );

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
