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
    toy::test_AnyView();
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
    auto sample_count = VK_SAMPLE_COUNT_8_BIT;
    toy::throwf(
      (rd::vk::Image::getAvailableSampleCounts() | sample_count) > 0,
      "the sample count is not available"
    );

    auto render_pass_info = rd::vk::RenderPassInfo{
      .extent = swapchain.extent(),
      .attachments = {
        rd::vk::AttachmentInfo{
          .format = swapchain.format(),
          .sample_count = sample_count,
        },
        rd::vk::AttachmentInfo{
          .format = swapchain.format(),
          .sample_count = VK_SAMPLE_COUNT_1_BIT,
        },
        rd::vk::AttachmentInfo{
          .format = depth_format,
          .sample_count = sample_count,
        },
      },
      .subpasses = {
        rd::vk::SubpassInfo{
          .colors = {0},
          .inputs = {},
          .multi_sample = {{
            .resolves = {1},
            .sample_count = sample_count,
          }},
          .depst_attachment = 2,
          .depth_option = {{
            .compare_op = VK_COMPARE_OP_LESS,
            .overwrite = true,
          }},
          .stencil_option = {},
          .vertex_shader_name = "hello.vert",
          .frag_shader_name = "hello.frag",
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
          .vertex_info = model::Vertex::getVertexInfo(),
          .descriptor_sets = {
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
                  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .count = 1,
              }},
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

    auto render_pass = rd::vk::RenderPass{ std::move(render_pass_info) };

    auto dset_pool = rd::vk::DescritporPool{
      3,
      std::vector{
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
        VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
      }
    };
    auto dset_model = rd::vk::DescriptorSet{ dset_pool, render_pass[0], 0 };
    auto model_data = trans::model::create();
    auto model_uniform = rd::vk::UniformBuffer{ model_data };
    dset_model[0] = model_uniform;

    auto dset_camera = rd::vk::DescriptorSet{ dset_pool, render_pass[0], 1 };
    auto view_data = trans::view::create(glm::vec3{ 0.0f });
    auto view_uniform = rd::vk::UniformBuffer{ view_data };
    auto proj_data = trans::proj::perspective({
      .width = swapchain.extent().width,
      .height = swapchain.extent().height,
    });
    auto proj_uniform = rd::vk::UniformBuffer{ proj_data };
    dset_camera[0] = view_uniform;
    dset_camera[1] = proj_uniform;

    auto dset_texture = rd::vk::DescriptorSet{ dset_pool, render_pass[0], 2 };
    auto sampled_texture = rd::SampledTexture{ "textures/texture.jpg", true };
    dset_texture[0] = sampled_texture;

    auto [vertexes, indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buffer = rd::VertexBuffer{ vertexes };
    auto index_buffer = rd::IndexBuffer{ indices };


    auto deep_image = rd::vk::Image{
      VK_FORMAT_D32_SFLOAT,
      swapchain.extent().width,
      swapchain.extent().height,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      VK_IMAGE_ASPECT_DEPTH_BIT,
      1,
      sample_count,
    };

    // auto app = app::Application{};
    // app.runLoop();
    while (!glfwWindowShouldClose(glfw::Window::getInstance())) {
      input_processor.processInput(16.6);
      if (swapchain.needRecreate()) {
        swapchain.recreate();
      }
      if (swapchain.valid()) {
      }
    }
  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
