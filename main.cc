import "vulkan_config.h";

import std;
import toy;
import vulkan;
import glm;

using namespace vk;

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
  InstanceWithDebugMessenger<toy::enable_debug> _instance;

  PhysicalDeviceInfo _physical_device_info;
  Surface            _surface;
  Device             _device;

  Swapchain              _swapchain;
  VkExtent2D             _extent;
  std::vector<ImageView> _image_views;

  ImageResource _depth_image;

  RenderPass               _render_pass;
  std::vector<Framebuffer> _framebuffers;

  DescriptorSetLayout _uniform_set_layout;
  DescriptorSetLayout _sampler_set_layout;
  PipelineResource    _pipeline_resource;

  DescriptorPool  _descriptor_pool;
  DescriptorSets  _descriptor_sets;
  VkDescriptorSet _sampler_set;

  struct ExecuteContext {
    VkQueue        queue;
    uint32_t       family_index;
    CommandPool    command_pool;
    CommandBuffers cmdbufs;
  };

  ExecuteContext _graphic_ctx;
  ExecuteContext _present_ctx;
  ExecuteContext _transfer_ctx;

  struct Worker {
    VkCommandBuffer cmdbuf;
    VkCommandBuffer cmdbuf_for_signal;
    Semaphore       image_available_sema;
    Semaphore       render_finished_sema;
    Semaphore       ownership_transfer_finished_sema;
    Fence           queue_batch_fence;
    Buffer          uniform_buffer;
    Memory          uniform_memory;
    void*           uniform_memory_map;
    VkDescriptorSet uniform_set;
  };
  std::vector<Worker> _workers;

  void updateUniformBuffer(const Worker& worker);

  int _in_flight_index;

  bool _last_present_failed;

  VertexBuffer _vertex_buffer;
  IndexBuffer  _index_buffer;
  uint32_t     _indices_count;

  struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
  };

  SampledTexture _sampled_texture;
};

VulkanApplication::VulkanApplication(uint32_t width, uint32_t height, std::string_view app_name)
  : _in_flight_index(0), _last_present_failed(false) {
  window_ = Window{ width, height, app_name };

  if constexpr (toy::enable_debug) {
    _instance.debug_config = DebugMessengerConfig{
      .message_severity_level = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,
      .message_type_flags = VK_DEBUG_UTILS_MESSAGE_TYPE_FLAG_BITS_MAX_ENUM_EXT,
    };
    _instance.instance = createInstance(app_name, _instance.debug_config);
    _instance.debug_messenger = createDebugMessenger(_instance.instance, _instance.debug_config);
  } else {
    _instance.instance = createInstance(app_name);
  }

  _surface = createSurface(_instance.instance, window_);

  // VK_KHR_SWAPCHAIN_EXTENSION_NAME 对应的扩展用于支持交换链
  auto required_device_extensions = std::array{ VK_KHR_SWAPCHAIN_EXTENSION_NAME };
  _physical_device_info = pickPhysicalDevice(
    _instance.instance,
    _surface,
    required_device_extensions,
    checkPhysicalDeviceSupport,
    checkSurfaceSupport,
    std::array<QueueFamilyChecker, 3>{ checkGraphicQueue, checkPresentQueue, checkTransferQueue }
  );
  toy::debug(_physical_device_info.queue_indices);
  toy::debugf(
    "graphic family index {} transfer family index",
    _physical_device_info.queue_indices[0].first == _physical_device_info.queue_indices[2].first
      ? "=="
      : "!="
  );

  int worker_count = 2;
  {
    auto [device, queues] = createDevice(_physical_device_info, required_device_extensions);
    _device = std::move(device);
    auto get_ctx = [&](auto pair, int cmdbuf_count) {
      auto ret = ExecuteContext{
        .queue = pair.first,
        .family_index = pair.second,
      };
      if (cmdbuf_count != 0) {
        ret.command_pool = createCommandPool(_device, pair.second);
        ret.cmdbufs = allocateCommandBuffers(_device, ret.command_pool, cmdbuf_count);
      }
      return ret;
    };

    _graphic_ctx = get_ctx(queues[0], worker_count * 2 + 1);
    _present_ctx = get_ctx(queues[1], 0);
    _transfer_ctx = get_ctx(queues[2], 3);
  }

  std::tie(_swapchain, _extent) = createSwapchain(
                                    _surface,
                                    _device,
                                    _physical_device_info.capabilities,
                                    _physical_device_info.surface_format,
                                    _physical_device_info.present_mode,
                                    window_,
                                    VK_NULL_HANDLE
  )
                                    .value();
  _image_views =
    createSwapchainImageViews(_device, _swapchain, _physical_device_info.surface_format.format);
  _depth_image = createDepthImage(_physical_device_info.device, _device, _extent);

  _render_pass =
    createRenderPass(_device, _physical_device_info.surface_format.format, _depth_image.format);

  _framebuffers =
    _image_views | views::transform([&](const auto& image_view) {
      return createFramebuffer(_render_pass, _device, _extent, image_view, _depth_image.image_view);
    }) |
    ranges::to<std::vector>();
  _uniform_set_layout = createUniformDescriptorSetLayout(_device);
  _sampler_set_layout = createSamplerDescriptorSetLayout(_device);

  using Vertex2D = Vertex<glm::vec3, glm::vec3, glm::vec2>;
  auto vertex_info = Vertex2D::getVertexInfo();
  _pipeline_resource = createGraphicsPipeline(
    _device,
    _render_pass,
    std::array{ vertex_info.binding_description },
    vertex_info.attribute_descriptions,
    std::array{ _uniform_set_layout.get(), _sampler_set_layout.get() }
  );

  _descriptor_pool = createDescriptorPool(
    _device,
    worker_count + 1,
    std::array{ VkDescriptorPoolSize{
                  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                  .descriptorCount = (uint32_t)worker_count,
                },
                VkDescriptorPoolSize{
                  .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  .descriptorCount = 1,
                } }
  );
  _descriptor_sets = allocateDescriptorSets(
    _device,
    _descriptor_pool,
    views::join(std::array{ views::repeat(_sampler_set_layout.get(), 1),
                            views::repeat(_uniform_set_layout.get(), worker_count) }) |
      ranges::to<std::vector>()
  );
  _sampler_set = _descriptor_sets.get()[0];
  // todo: use chunk
  auto chunked_cmdbufs =
    _graphic_ctx.cmdbufs.get() | views::drop(1) | toy::enumerate |
    toy::chunkBy([](auto a, auto b) { return b.first % 2 != 0; }) |
    views::transform([](const auto& sub_range) {
      return sub_range | views::transform([](auto pair) { return pair.second; });
    });
  _workers = views::zip(chunked_cmdbufs, _descriptor_sets.get() | views::drop(1)) |
             views::transform([&](auto pair) {
               auto& [cmdbufs, descriptor_set] = pair;
               toy::debugf("size of cmdbufs: {}", cmdbufs.size());
               auto [uniform_buffer, uniform_memory] = createBuffer(
                 _physical_device_info.device,
                 _device,
                 sizeof(UniformBufferObject),
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
               );
               void* map;
               vkMapMemory(_device, uniform_memory, 0, sizeof(UniformBufferObject), 0, &map);
               updateDescriptorSet(_device, descriptor_set, uniform_buffer.get());
               return Worker{
                 .cmdbuf = cmdbufs[0],
                 .cmdbuf_for_signal = cmdbufs[1],
                 .image_available_sema = createSemaphore(_device),
                 .render_finished_sema = createSemaphore(_device),
                 .ownership_transfer_finished_sema = createSemaphore(_device),
                 .queue_batch_fence = createFence(_device, true),
                 .uniform_buffer = std::move(uniform_buffer),
                 .uniform_memory = std::move(uniform_memory),
                 .uniform_memory_map = map,
                 .uniform_set = descriptor_set,
               };
             }) |
             ranges::to<std::vector>();
  auto vertex_data = std::array<Vertex2D, 8>{
    Vertex2D{ { -0.5f, -0.5f, +0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
    { { +0.5f, -0.5f, +0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
    { { +0.5f, +0.5f, +0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
    { { -0.5f, +0.5f, +0.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
    { { +0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
    { { +0.5f, +0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
    { { -0.5f, +0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
  };
  auto vertex_indices = std::array<uint16_t, 12>{
    0, 1, 2, 2, 3, 0, 0 + 4, 1 + 4, 2 + 4, 2 + 4, 3 + 4, 0 + 4,
  };

  auto family_transfer = std::optional<FamilyTransferInfo>{};
  if (_transfer_ctx.family_index != _graphic_ctx.family_index) {
    family_transfer = FamilyTransferInfo{ _transfer_ctx.family_index, _graphic_ctx.family_index };
  }
  _vertex_buffer = VertexBuffer{ _physical_device_info.device,   _device,     _transfer_ctx.queue,
                                 _transfer_ctx.cmdbufs.get()[0], vertex_data, family_transfer };
  _index_buffer = IndexBuffer{ _physical_device_info.device,   _device,        _transfer_ctx.queue,
                               _transfer_ctx.cmdbufs.get()[1], vertex_indices, family_transfer };
  _indices_count = vertex_indices.size();
  _sampled_texture = SampledTexture{ _physical_device_info.device,
                                     _device,
                                     _transfer_ctx.queue,
                                     _transfer_ctx.cmdbufs.get()[2],
                                     _sampler_set,
                                     _physical_device_info.properties.limits.maxSamplerAnisotropy,
                                     family_transfer };
  recordAndSubmit(
    _graphic_ctx.cmdbufs.get()[0],
    _graphic_ctx.queue,
    [&](auto cmdbuf) {
      _sampled_texture.recordDstFamilyTransfer(cmdbuf);
      _vertex_buffer.recordDstFamilyTransfer(cmdbuf);
      _index_buffer.recordDstFamilyTransfer(cmdbuf);
    },
    std::array{
      _sampled_texture.getNeedWaitInfo(),
      _vertex_buffer.getNeedWaitInfo(),
      _index_buffer.getNeedWaitInfo(),
    },
    {},
    VK_NULL_HANDLE
  );
}

VulkanApplication::~VulkanApplication() {
  vkDeviceWaitIdle(_device);
  for (auto& worker : _workers) {
    vkUnmapMemory(_device, worker.uniform_memory);
  }
}

auto VulkanApplication::recreateSwapchain() -> bool {
  // todo: 换成范围更小的约束
  vkDeviceWaitIdle(_device);
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    _physical_device_info.device, _surface, &_physical_device_info.capabilities
  );

  if (auto ret = createSwapchain(
        _surface,
        _device,
        _physical_device_info.capabilities,
        _physical_device_info.surface_format,
        _physical_device_info.present_mode,
        window_,
        _swapchain
      );
      ret.has_value()) {
    // move old resource to sorted declared variable to guarantee destroy order
    auto old_swap_chain = std::move(_swapchain);
    auto old_image_views = std::move(_image_views);
    auto old_depth_image = std::move(_depth_image);
    auto old_framebuffers = std::move(_framebuffers);

    std::tie(_swapchain, _extent) = std::move(ret.value());
    _image_views =
      createSwapchainImageViews(_device, _swapchain, _physical_device_info.surface_format.format);
    _depth_image = createDepthImage(_physical_device_info.device, _device, _extent);
    _framebuffers = _image_views | views::transform([&](const auto& image_view) {
                      return createFramebuffer(
                        _render_pass, _device, _extent, image_view, _depth_image.image_view
                      );
                    }) |
                    ranges::to<std::vector>();
    return true;
  } else if (ret.error() == SwapchainCreateError::EXTENT_ZERO) {
    return false;
  } else {
    toy::throwf("create swapchain return some error different to "
                "SwapchainCreateError::EXTENT_ZERO");
  }
  std::unreachable();
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
      glm::perspective(glm::radians(45.0f), _extent.width * 1.0f / _extent.height, 0.1f, 10.0f),
  };
  ubo.proj[1][1] *= -1;
  std::memcpy(worker.uniform_memory_map, reinterpret_cast<void*>(&ubo), sizeof(ubo));
}

void VulkanApplication::drawFrame() {
  auto&    worker = _workers[_in_flight_index];
  uint32_t image_index;

  auto wait_fence = worker.queue_batch_fence.get();
  vkWaitForFences(_device, 1, &wait_fence, VK_TRUE, std::numeric_limits<uint64_t>::max());

  if (auto result = vkAcquireNextImageKHR(
        _device,
        _swapchain,
        std::numeric_limits<uint64_t>::max(),
        worker.image_available_sema,
        VK_NULL_HANDLE,
        &image_index
      );
      result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || _last_present_failed) {
    if (recreateSwapchain()) {
      _last_present_failed = false;
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
  vkResetFences(_device, 1, &wait_fence);
  updateUniformBuffer(worker);
  // vkBeginCommandBuffer 会隐式执行vkResetCommandBuffer
  // vkResetCommandBuffer(worker.command_buffer, 0);
  recordAndSubmit(
    worker.cmdbuf,
    _graphic_ctx.queue,
    [&](VkCommandBuffer cmdbuf) {
      recordCommandBuffer(
        cmdbuf,
        _render_pass,
        _pipeline_resource.pipeline,
        _extent,
        _framebuffers[image_index],
        _vertex_buffer,
        _index_buffer,
        _pipeline_resource.pipeline_layout,
        std::array{ worker.uniform_set, _sampler_set }
      );
    },
    std::array<std::pair<VkSemaphore, VkPipelineStageFlags>, 1>{ std::pair{
      worker.image_available_sema.get(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT } },
    {},
    VK_NULL_HANDLE
  );
  // vkQueueSubmit的fence的signal操作会在提交顺序之前的所有命令执行完毕后执行
  recordAndSubmit(
    worker.cmdbuf_for_signal,
    _graphic_ctx.queue,
    [](auto _) {},
    {},
    std::array{ worker.render_finished_sema.get() },
    worker.queue_batch_fence
  );

  auto present_info = VkPresentInfoKHR{
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &worker.render_finished_sema.get(),
    .swapchainCount = 1,
    .pSwapchains = &_swapchain.get(),
    .pImageIndices = &image_index,
    // .pResults: 当有多个 swapchain 时检查每个的result
  };
  if (auto result = vkQueuePresentKHR(_present_ctx.queue, &present_info);
      result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    toy::debugf(
      "the queue present return {}",
      result == VK_ERROR_OUT_OF_DATE_KHR ? "out of date error" : "sub optimal"
    );
    _last_present_failed = true;
  } else {
    checkVkResult(result, "present");
  }
  _in_flight_index = (_in_flight_index + 1) % _workers.size();
}

int main() {
  std::print("hello, world!");
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
