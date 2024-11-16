#include <toy.h>
import toy;
// import application;
import std;

import <vulkan_config.h>;
import <glfw_config.h>;
import render;
import context;
import glm;
import input;
import model;
import glfw;
import transform;
import gui;
import loop;
// import pipeline.outline;
import pipeline.base2;
import pipeline.uniform;
import render;
// import pipeline.resources;
// import pipeline.drawunit;
// import tool.move;
import camera;
import drag;

using Vertex = model::Vertex;
constexpr auto dset_layout = rd::meta::DescriptorLayout{
  rd::meta::UniformBinding{ rd::meta::type<rd::align::mat4>(), rd::meta::Stage::VERTEX },
  rd::meta::TextureBinding{ rd::meta::Stage::FRAGMENT },
};

void func(const int& a) {}

int main() {
  try {
    json::test_json();
    toy::test_Generator::test();
    toy::test_EnumSet::test();
    trans::test_trans();
    auto  ctx = ctx::Context{ "hello vulkan", 1920, 1080 };
    auto  dset_pools = rd::meta::DescriptorPools{};
    auto& input = input::InputProcessor::getInstance();

    auto pipeline = pl::PipelineInfo{
      .colors = { 0 },
      .depst =
        rd::DepstOption{
          .attachment = 0,
          .depth =
            rd::DepthOption{
              .compare_op = VK_COMPARE_OP_LESS,
              .overwrite = true,
            },
          .stencil =
            rd::StencilOption{
              .front =
                VkStencilOpState{
                  // set the dynamic reference to attachment used to compare by subpass[1]
                  .passOp = VK_STENCIL_OP_REPLACE,
                  .compareOp = VK_COMPARE_OP_ALWAYS,
                  .writeMask = 0xFFFFFFFF,
                },
              .dyn_ref = true,
            },
        },
      .vertex_shader_name = "hello.vert",
      .frag_shader_name = "hello.frag",
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .cull_mode = VK_CULL_MODE_BACK_BIT,
      .vertex_info = Vertex::getVertexInfo(),
      .dset_layout = dset_pools.getPool<dset_layout>(),
    };
    auto render_pass_info = pl::RenderPassInfo{
      .depst_formats = { VK_FORMAT_D24_UNORM_S8_UINT },
      .multi_sampling = VK_SAMPLE_COUNT_8_BIT,
      .pipelines = { pipeline },
    };
    auto render_pass = pl::RenderPass{ render_pass_info };
    // render_pass.setDepstClearValue(0, VkClearDepthStencilValue{ .depth = 1.0f });
    render_pass.setDepstClearValue(0, { 1.0f, 0 });
    render_pass.setColorClearValue(0, { 1.0f, 1.0f, 1.0f, 1.0f });

    auto camera = camera::Camera{};
    auto controller = camera::Controller{ &camera.getView() };

    auto texture = rd::SampledTexture::create(
      "model/viking_room.png", true, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    auto uniform = pl::UniformBuffer<rd::align::mat4>{ { trans::model::create() } };
    auto [vertexes, indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buf = rd::VertexBuffer{ vertexes };
    auto index_buf = rd::IndexBuffer{ indices };
    auto draw_unit = pl::DrawUnit{
      0, &vertex_buf, &index_buf, rd::meta::DescriptorSet<dset_layout>{ &uniform, &texture }
    };
    render_pass.setCamera(&camera.getData());

    loop::Loop::getInstance().run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
