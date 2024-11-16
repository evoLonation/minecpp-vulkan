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
import pipeline.base;
import pipeline.uniform;
// import pipeline.resources;
// import pipeline.drawunit;
// import tool.move;
import camera;
import drag;

using Vertex = model::Vertex;
constexpr auto dset_layout = pl::DescriptorLayout{
  pl::UniformBinding{ pl::type<rd::align::mat4>(), pl::Stage::VERTEX },
  pl::TextureBinding{ pl::Stage::FRAGMENT },
};
constexpr auto pipeline_layout = pl::PipelineLayout{
  .vertex = pl::type<Vertex>{},
  .dset_layout = dset_layout,
  .color_n = 1,
  .depst = true,
};

void func(const int& a) {}

int main() {
  try {
    json::test_json();
    toy::test_Generator::test();
    toy::test_EnumSet::test();
    trans::test_trans();
    auto  ctx = ctx::Context{ "hello vulkan", 1920, 1080 };
    auto  dset_pools = pl::DescriptorPools{};
    auto  draw_units = pl::DrawUnits{};
    auto& input = input::InputProcessor::getInstance();

    auto camera = camera::Camera{};
    auto controller = camera::Controller{ &camera.getView() };

    auto texture = rd::SampledTexture::create(
      "model/viking_room.png", true, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );
    auto uniform = pl::UniformBuffer<rd::align::mat4>{ { trans::model::create() } };
    auto [vertexes, indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buf = pl::VertexBuffer<Vertex>{ vertexes };
    auto index_buf = pl::IndexBuffer{ indices };
    auto draw_unit =
      pl::DrawUnit<pipeline_layout>{ &vertex_buf, &index_buf, { &uniform, &texture } };

    auto subpass = pl::PipelineInfo<pipeline_layout>{
      .colors = { 0 },
      .depst =
        rd::DepstOption{
          .attachment = 0,
          .depth = { {
            .compare_op = VK_COMPARE_OP_LESS,
            .overwrite = true,
          } },
          .stencil = { {
            .front =
              VkStencilOpState{
                // set the dynamic reference to attachment used to compare by subpass[1]
                .passOp = VK_STENCIL_OP_REPLACE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .writeMask = 0xFFFFFFFF,
              },
            .dyn_ref = false,
          } },
        },
      .vertex_shader_name = "hello.vert",
      .frag_shader_name = "hello.frag",
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .cull_mode = VK_CULL_MODE_BACK_BIT,
    };
    auto render_pass_info = pl::RenderPassInfo{
      .depst_formats = { VK_FORMAT_D24_UNORM_S8_UINT },
      .multi_sampling = VK_SAMPLE_COUNT_8_BIT,
      .pipelines = std::tuple{ subpass },
    };
    auto render_pass = pl::RenderPass{ render_pass_info };
    render_pass.setCamera(&camera.getData());
    // render_pass.setDepstClearValue(0, VkClearDepthStencilValue{ .depth = 1.0f });
    render_pass.setDepstClearValue(0, { 1.0f, 0 });
    render_pass.setColorClearValue(0, { 1.0f, 1.0f, 1.0f, 1.0f });

    // auto pipeline = pl::BasicPipeline{};
    // auto controller = camera::Controller{ &camera.getView() };
    // auto draw_unit_0 = pl::DrawUnit{ 1 };
    // auto draw_unit_1 = pl::DrawUnit{ 2, glm::vec3{ 1.0f, 1.0f, 1.0f } };
    // auto draw_unit_2 = pl::DrawUnit{ 3, glm::vec3{ -1.0f, -1.0f, -1.0f } };
    // // auto cube_0 = tool::Cube{ glm::vec3{ 1.0f, 1.0f, 1.0f } };
    // // auto cone_0 = tool::Cone{ glm::vec3{ 0.0f, 0.0f, 1.0f } };
    // auto axis_0 = tool::Axis{ glm::vec3{ 1.0f, 0.0f, 1.0f } };
    // pipeline.setCamera(&camera.getData());
    // pipeline.addDrawUnit(&draw_unit_0);
    // pipeline.addDrawUnit(&draw_unit_1);
    // pipeline.addDrawUnit(&draw_unit_2);
    // // pipeline.addDrawUnit(&cube_0);
    // // pipeline.addDrawUnit(&cone_0);
    // pipeline.addDrawUnit(&axis_0);

    loop::Loop::getInstance().run();

  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
