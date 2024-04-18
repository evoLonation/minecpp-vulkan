import "vulkan_config.h";
import "glfw_config.h";

import std;
import toy;
import vulkan;
import glfw;
import glm;
import model;
import input;
import render;

using namespace vk;
using namespace glfw;

struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

auto getUniform(int index) {
  static auto startTime = std::chrono::high_resolution_clock::now();

  auto  width = 1920;
  auto  height = 1080;
  auto  currentTime = std::chrono::high_resolution_clock::now();
  float time =
    std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
  auto ubo = UniformBufferObject{
    .model = glm::rotate(
      glm::translate(glm::mat4(1.0f), glm::vec3{ index, 0.0f, 0.0f }),
      time * glm::radians(90.0f),
      glm::vec3(0.0f, 0.0f, 1.0f)
    ),
    .view = glm::lookAt(
      glm::vec3(5.0f, 5.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)
    ),
    .proj = glm::perspective(glm::radians(45.0f), width * 1.0f / height, 0.1f, 10.0f),
  };
  ubo.proj[1][1] *= -1;
  return ubo;
}

int main() {
  // model::getModelInfo("model/viking_room.obj");
  // return 0;
  try {
    toy::test_EnumerateAdaptor();
    toy::test_SortedRange();
    toy::test_ChunkBy();
    toy::test_CommonView();
    auto applicationName = "hello, vulkan!";
    auto width = 1920;
    auto height = 1080;
    auto ctx = render::Context{ applicationName, width, height };
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();
    ImGui_ImplGlfw_InitForVulkan(ctx.window, true);
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = ctx.instance.instance;
    init_info.PhysicalDevice = ctx.pdevice_info.device;
    init_info.Device = ctx.device;
    init_info.QueueFamily = ctx.graphic_ctx.family_index;
    init_info.Queue = ctx.graphic_ctx.queue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    VkDescriptorPool ds_pool;
    {

      VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
      };
      VkDescriptorPoolCreateInfo pool_info = {};
      pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
      pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
      pool_info.maxSets = 1;
      pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
      pool_info.pPoolSizes = pool_sizes;
      vkCreateDescriptorPool(ctx.device, &pool_info, nullptr, &ds_pool);
    }
    init_info.DescriptorPool = ds_pool;
    init_info.RenderPass = ctx.render_pass;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = [](VkResult err) {
      if (err == 0)
        return;
      toy::debugf("[vulkan] Error: VkResult = {}", (int)err);
      if (err < 0)
        abort();
    };
    ImGui_ImplVulkan_Init(&init_info);

    auto pipeline = render::Pipeline{ "hello.vert",
                                      "hello.frag",
                                      model::Vertex::getVertexInfo(),
                                      std::array{ render::ResourceType::UNIFORM,
                                                  render::ResourceType::SAMPLER } };
    auto [vertex_data, vertex_indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buffer = render::VertexBuffer{ std::span<const model::Vertex>{ vertex_data } };
    auto index_buffer = render::IndexBuffer{ vertex_indices };
    auto sampled_texture = render::SampledTexture{ "model/viking_room.png" };

    int  unit_count = 8;
    auto uniform_datas = std::vector<UniformBufferObject>(unit_count);
    auto uniforms = std::vector<render::Uniform<UniformBufferObject>>{};
    uniforms.reserve(unit_count);
    auto draw_units = std::vector<render::DrawUnit>{};
    for (auto& uniform_data : uniform_datas) {
      uniforms.emplace_back(uniform_data);
      draw_units.emplace_back(
        pipeline,
        vertex_buffer,
        index_buffer,
        std::array<render::DescriptorResource*, 2>{ &uniforms.back(), &sampled_texture }
      );
    }
    auto resource_register = render::ResourceRegister{ std::array<render::DeviceLocalResource*, 3>{
      &vertex_buffer, &index_buffer, &sampled_texture } };
    auto drawer = render::Drawer{ pipeline };
    for (auto [unit, uniform] : views::zip(draw_units, uniforms)) {
      drawer.registerUnit(unit);
      drawer.registerUniform(uniform);
    }

    auto input_processor = input::InputProcessor{ ctx.window };
    input_processor.addKeyDownHandler(GLFW_KEY_A, []() { toy::debug("press down A"); });
    input_processor.addKeyHoldHandler(GLFW_KEY_A, [](int time) {
      toy::debugf("press hold A {}", time);
    });
    input_processor.addKeyReleaseHandler(GLFW_KEY_A, [](int time) {
      toy::debugf("press release A when hold {}", time);
    });
    while (!glfwWindowShouldClose(ctx.window)) {
      for (auto i : views::iota(0, unit_count)) {
        uniform_datas[i] = getUniform(i - 4);
      }
      input_processor.processInput();
      // Start the Dear ImGui frame
      ImGui_ImplVulkan_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can
      // browse its code to learn more about Dear ImGui!).
      ImGui::ShowDemoWindow();
      ImGui::Render();
      auto* draw_data = ImGui::GetDrawData();
      drawer.draw(draw_data);
    }
  } catch (const std::exception& e) {

    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
