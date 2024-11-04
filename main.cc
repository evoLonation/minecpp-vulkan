#include <toy.h>
import toy;
// import application;
import std;

import <vulkan_config.h>;
import <glfw_config.h>;
import render.instance;
import render.device;
import render.surface;
import render.resource;
import render.executor;
import render.queue_requestor;
import render.image;
import render.render_pass;
import render.descriptor;
import render.buffer;
import render.framebuffer;
import render.presentation;
import render.context;
import render.sync;
import render.tracker;
import render.sampler;
import render.vertex;
import glm;
import input;
import model;
import glfw;
import transform;
import gui;

int main() {
  try {
    json::test_json();
    toy::test_Generator::test();
    toy::test_EnumSet::test();
    trans::test_trans();
    auto  ctx = rd::Context{ "hello vulkan", 1920, 1080 };
    auto& input = input::InputProcessor::getInstance();
    auto  gui_ctx = gui::Context{};

    auto depth_format = VK_FORMAT_D32_SFLOAT;
    auto sample_count = VK_SAMPLE_COUNT_8_BIT;
    TOY_ASSERT((rd::ImageContext::getInstance().getAvailableSampleCounts() & sample_count) > 0);

    auto  presentation = rd::Presentation{ ctx._surface->get() };
    auto& swapchain = presentation.getSwapchain();
    toy::throwf(swapchain.isValid(), "the swapchain is not valid");

    using BindingInfo = rd::DescriptorSetLayout::BindingInfo;
    auto dset_pool_model = rd::DescriptorPool{ std::vector{ BindingInfo{
      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .count = 1,
    } } };
    auto dset_pool_camera = rd::DescriptorPool{ std::vector{
      BindingInfo{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .count = 1,
      },
      BindingInfo{
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .count = 1,
      },
    } };
    auto dset_pool_texture = rd::DescriptorPool{ std::vector{ BindingInfo{
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .count = 1,
    } } };

    auto attachment_infos = std::vector<rd::AttachmentInfo>{
      rd::AttachmentInfo{
        .format = swapchain.getFormat(),
        .sample_count = sample_count,
        .keep_old_content = false,
        .keep_new_content = false,
      },
      rd::AttachmentInfo{
        .format = swapchain.getFormat(),
        .sample_count = VK_SAMPLE_COUNT_1_BIT,
        .keep_old_content = false,
        .keep_new_content = true,
      },
      rd::AttachmentInfo{
        .format = depth_format,
        // .sample_count = VK_SAMPLE_COUNT_1_BIT,
        .sample_count = sample_count,
        .keep_old_content = false,
        .keep_new_content = false,
      },
    };
    auto subpass_infos = std::vector<rd::SubpassPipelineInfo>{
      rd::SubpassPipelineInfo{
        .colors = {0},
        .inputs = {},
        .multi_sample = {{
          .resolves = {1},
          .sample_count = sample_count,
        }},
        .depst = {{
          .attachment = 2,
          .depth_option = {
            .compare_op = VK_COMPARE_OP_LESS,
            .overwrite = false,
          },
        }},
        .vertex_shader_name = "hello.vert",
        .frag_shader_name = "hello.frag",
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .vertex_info = model::Vertex::getVertexInfo(),
        .dset_layouts = { dset_pool_model, dset_pool_camera, dset_pool_texture },
      }
    };

    auto model_data = trans::model::create(glm::vec3{ 0.0f, 0.0f, 0.0f });
    // model_data = model_data * trans::rotate<trans::Axis::Z>(90.0f);
    auto model_uniform = rd::UniformBuffer{ model_data };
    auto view_data = trans::view::create(glm::vec3{ 5.0f, 5.0f, 5.0f });
    auto view_uniform = rd::UniformBuffer{ view_data };
    auto proj_data = trans::proj::perspective({
      .width = swapchain.getExtent().width,
      .height = swapchain.getExtent().height,
    });
    auto proj_uniform = rd::UniformBuffer{ proj_data };
    auto sampled_texture = rd::SampledTexture::create(
      "model/viking_room.png", true, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    auto [vertexes, indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buffer = rd::VertexBuffer{ vertexes };
    auto index_buffer = rd::IndexBuffer{ indices };

    auto render_pass = rd::RenderPassPipeline{ attachment_infos, subpass_infos };
    auto dset_model = rd::ResourceSet{ &dset_pool_model, { &model_uniform } };
    auto dset_camera = rd::ResourceSet{ &dset_pool_camera, { &view_uniform, &proj_uniform } };
    auto dset_texture = rd::ResourceSet{ &dset_pool_texture, { &sampled_texture } };

    struct FramebufferResource {
      rd::FrameImage sample_image;
      rd::FrameImage depth_image;
    };

    auto createFramebuffers = [&]() {
      auto sample_image = rd::FrameImage{
        swapchain.getFormat(),
        swapchain.getExtent().width,
        swapchain.getExtent().height,
        rd::FrameImage::Type::COLOR,
        sample_count,
      };
      auto depth_image = rd::FrameImage{
        depth_format,
        swapchain.getExtent().width,
        swapchain.getExtent().height,
        rd::FrameImage::Type::DEPTH_STENCIL,
        sample_count,
      };
      return FramebufferResource{
        .sample_image = std::move(sample_image),
        .depth_image = std::move(depth_image),
      };
    };
    auto framebuffers = createFramebuffers();

    auto createResource = [&]() {
      proj_data = trans::proj::perspective({
        .width = swapchain.getExtent().width,
        .height = swapchain.getExtent().height,
      });
      proj_uniform.update();
      framebuffers = createFramebuffers();
    };

    auto clear_values = std::vector{
      VkClearValue{ .color = { .float32 = { 0.5f, 0.5f, 0.5f, 1.0f } } },
      VkClearValue{ .color = { .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } } },
      VkClearValue{ .depthStencil = { .depth = 1.0f, } },
    };

    auto  count = 0;
    auto  last_time = chrono::high_resolution_clock::now();
    float interval;
    while (!glfwWindowShouldClose(glfw::Window::getInstance())) {
      auto interval_ = chrono::high_resolution_clock::now() - last_time;
      last_time += interval_;
      interval = chrono::duration_cast<chrono::microseconds>(interval_).count() / 1'000'000.0f;
      input.processInput(interval);
      auto res = presentation.prepare();
      if (!res.has_value()) {
        if (presentation.recreate()) {
          toy::debugf("recreate success");
          createResource();
        }
      } else {
        using Keyboard = input::Keyboard;
        using ButtonState = input::ButtonState;
        if (auto opt = input.getState(Keyboard::KEY_A); opt && opt->state == ButtonState::DOWN) {
          model_data = model_data * trans::rotate<trans::Axis::Z>(90.0f);
          model_uniform.update();
        }

        auto& context = res.value();
        render_pass.setRecorder(0, [&](rd::PipelineDrawer drawer) {
          drawer.bindVertexBuffer(&vertex_buffer);
          drawer.bindIndexBuffer(&index_buffer);
          drawer.bindResourceSet(0, &dset_model);
          drawer.bindResourceSet(1, &dset_camera);
          drawer.bindResourceSet(2, &dset_texture);
          drawer.draw();
        });
        auto attachments = std::array<rd::FrameImageManager*, 3>{
          &framebuffers.sample_image,
          context.image_manager,
          &framebuffers.depth_image,
        };
        render_pass.recordDraw(attachments, clear_values);
        gui_ctx.draw();
        gui_ctx.recordDraw(context.image_manager);
        presentation.present(context.image_index);
      }
      count++;
      if (count % 1000 == 0) {
        toy::debug(count);
      }
    }
  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
