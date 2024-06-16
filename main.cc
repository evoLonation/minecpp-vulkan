import toy;
// import application;
import std;

import "vulkan_config.h";
import "glfw_config.h";
import render.vk.instance;
import render.vk.device;
import render.vk.surface;
import render.vk.swapchain;
import render.vk.executor;
import render.vk.image;
import render.vk.render_pass;
import render.vk.buffer;
import render.vk.presentation;
import render.vk.sync;
import render.sampler;
import render.vertex;
import glm;
import input;
import model;
import glfw;
import transform;

int main() {
  try {
    json::test_json();
    toy::test_EnumerateAdaptor();
    toy::test_SortedRange();
    toy::test_ChunkBy();
    toy::test_Generator::test();
    toy::test_EnumSet::test();
    trans::test_trans();

    auto window = glfw::Window{ 1920, 1080, "hello vulkan" };

    auto input_processor = input::InputProcessor{};

    auto instance = rd::vk::Instance{ "hello" };

    auto surface = rd::vk::Surface{};

    auto device = rd::vk::Device{};

    auto swapchain = rd::vk::Swapchain{};

    auto executor = rd::vk::CommandExecutor{};

    auto depth_format = VK_FORMAT_D32_SFLOAT;
    auto sample_count = VK_SAMPLE_COUNT_2_BIT;
    toy::throwf(
      (rd::vk::Image::getAvailableSampleCounts() | sample_count) > 0,
      "the sample count is not available"
    );

    auto presentation =
      rd::vk::Presentation{ VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };

    auto render_pass_info = rd::vk::RenderPassInfo{
      .attachments = {
        // rd::vk::AttachmentInfo{
        //   .format = swapchain.format(),
        //   .sample_count = sample_count,
        // },
        rd::vk::AttachmentInfo{
          .format = swapchain.format(),
          .sample_count = VK_SAMPLE_COUNT_1_BIT,
          .enter_dep = {
            .scope = rd::vk::Scope{
              .stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            },
            .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          },
          .exit_dep = {
            .scope = rd::vk::Scope{
              .stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            },
            .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .keep_content = true,
          },
        },
        // rd::vk::AttachmentInfo{
        //   .format = depth_format,
        //   .sample_count = sample_count,
        // },
      },
      .subpasses = {
        rd::vk::SubpassInfo{
          .colors = {0},
          .inputs = {},
          .multi_sample = {},
          // .multi_sample = {{
          //   .resolves = {1},
          //   .sample_count = sample_count,
          // }},
          .depst_attachment = {},
          .depth_option = {},
          .stencil_option = {},
          .vertex_shader_name = "hello.vert",
          .frag_shader_name = "hello.frag",
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
          .vertex_info = model::Vertex::getVertexInfo(),
          // .vertex_info = Vertex::getVertexInfo(),
          .descriptor_sets = {
            rd::vk::DescriptorSetInfo{
              .descriptors = {
                rd::vk::DescriptorInfo{
                  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .count = 1,
                },
              },
            },
            rd::vk::DescriptorSetInfo{
              .descriptors = {
                rd::vk::DescriptorInfo{
                  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .count = 1,
                },
                rd::vk::DescriptorInfo{
                  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .count = 1,
                },
              },
            },
            rd::vk::DescriptorSetInfo{
              .descriptors = {rd::vk::DescriptorInfo{
                  .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .count = 1,
              }},
            },
          },
        },
      },
    };

    auto model_data = trans::model::create(glm::vec3{ 0.0f, 0.0f, 0.0f });
    model_data = model_data * trans::rotate<trans::Axis::Z>(90.0f);
    auto model_uniform = rd::vk::UniformBuffer{ model_data };
    auto view_data = trans::view::create(glm::vec3{ 5.0f, 5.0f, 5.0f });
    auto view_uniform = rd::vk::UniformBuffer{ view_data };
    auto proj_data = trans::proj::perspective({
      .width = swapchain.extent().width,
      .height = swapchain.extent().height,
    });
    auto proj_uniform = rd::vk::UniformBuffer{ proj_data };
    auto sampled_texture = rd::SampledTexture{ "model/viking_room.png", true };
    auto [vertexes, indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buffer = rd::VertexBuffer{ vertexes };
    auto index_buffer = rd::IndexBuffer{ indices };

    auto render_pass = rd::vk::RenderPass{ render_pass_info };
    auto dset_pool = rd::vk::DescriptorPool{
      3,
      std::vector{
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
      }
    };
    auto dset_model = rd::vk::DescriptorSet{ dset_pool, render_pass[0], 0 };
    dset_model[0] = model_uniform;
    auto dset_camera = rd::vk::DescriptorSet{ dset_pool, render_pass[0], 1 };
    dset_camera[0] = view_uniform;
    dset_camera[1] = proj_uniform;
    auto dset_texture = rd::vk::DescriptorSet{ dset_pool, render_pass[0], 2 };
    dset_texture[0] = sampled_texture;

    auto framebuffers =
      swapchain.image_views() | views::transform([&](VkImageView image_view) {
        return rd::vk::Framebuffer{ render_pass, swapchain.extent(), std::array{ image_view } };
      }) |
      ranges::to<std::vector>();

    auto recreateResource = [&]() {
      proj_data = trans::proj::perspective({
        .width = swapchain.extent().width,
        .height = swapchain.extent().height,
      });
      proj_uniform.update();
      framebuffers =
        swapchain.image_views() | views::transform([&](VkImageView image_view) {
          return rd::vk::Framebuffer{ render_pass, swapchain.extent(), std::array{ image_view } };
        }) |
        ranges::to<std::vector>();
    };

    auto clear_values = std::array{
      // VkClearValue{ .color = { .float32 = { 0.5f, 0.5f, 0.5f, 1.0f } } },
      VkClearValue{ .color = { .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } } },
      // VkClearValue{ .depthStencil = { .depth = 1.0f, } },
    };

    auto count = 0;
    while (!glfwWindowShouldClose(glfw::Window::getInstance())) {
      input_processor.processInput(16.6);
      if (auto res = presentation.prepare(); res.has_value()) {
        auto& context = res.value();
        if (context.need_recreate) {
          recreateResource();
        }
        render_pass[0].recorder = [&](rd::vk::Pipeline::Recorder& recorder) {
          recorder.init();
          recorder.vertex_buffer = vertex_buffer;
          recorder.index_buffer = index_buffer;
          recorder.descriptor_set[0] = dset_model;
          recorder.descriptor_set[1] = dset_camera;
          recorder.descriptor_set[2] = dset_texture;
          recorder.draw();
        };
        auto fence = rd::vk::executors::render.submit(
          [&](auto cmdbuf) {
            render_pass.recordDraw(cmdbuf, framebuffers[context.image_index], clear_values);
          },
          std::array{
            rd::vk::WaitSemaphore{ context.wait_sema, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT } },
          std::array{ context.signal_sema }
        );
        count++;
        if (count % 60 == 0) {
          toy::debug(count);
        }
        presentation.present();
        fence.wait();
      }
    }
  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
