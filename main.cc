import "vulkan_config.h";
import "glfw_config.h";
import "imgui_config.h";

import std;
import toy;
import vulkan;
import glfw;
import glm;
import model;
import render;
import gui;
import control;
import axis;
import transform;
import manager;

int main() {
  try {
    toy::test_EnumerateAdaptor();
    toy::test_SortedRange();
    toy::test_ChunkBy();
    toy::test_AnyView();
    toy::test_Generator::test();
    trans::test_trans();
    auto applicationName = "hello, vulkan!";
    auto width = 1920;
    auto height = 1080;
    auto ctx = render::Context{ applicationName, width, height, true };
    auto executor = render::CommandExecutor{};
    auto loop = render::Loop{};
    auto frame_ctx = render::FrameContext{};
    auto input_processor = render::InputProcessor{};
    auto exit_handler = render::KeyDownHandler(GLFW_KEY_ESCAPE, [&]() {
      input_processor.setCursorVisible(!input_processor.isCursorVisible());
    });
    auto drawer = render::Drawer{};
    auto gui_ctx = gui::Context{};
    auto executor_moniter = gui::CoDrawer{ control::cmdExecutorMoniter() };

    // auto pipeline = render::Pipeline{ "hello.vert",
    //                                   "hello.frag",
    //                                   VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    //                                   model::Vertex::getVertexInfo(),
    //                                   std::array{ render::ResourceType::UNIFORM,
    //                                               render::ResourceType::UNIFORM,
    //                                               render::ResourceType::UNIFORM,
    //                                               render::ResourceType::SAMPLER },
    //                                   std::nullopt };
    auto stencil_options = vk::getOutliningStencil();
    stencil_options.first.dynamic_reference = false;
    stencil_options.second.dynamic_reference = true;
    auto pipeline_outline_1 = render::Pipeline{ "hello.vert",
                                                "hello.frag",
                                                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                model::Vertex::getVertexInfo(),
                                                std::array{ render::ResourceType::UNIFORM,
                                                            render::ResourceType::UNIFORM,
                                                            render::ResourceType::UNIFORM,
                                                            render::ResourceType::SAMPLER },
                                                stencil_options.first };
    auto pipeline_outline_2 = render::Pipeline{ "outline.vert",
                                                "outline.frag",
                                                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                                                model::Vertex::getVertexInfo(),
                                                std::array{ render::ResourceType::UNIFORM,
                                                            render::ResourceType::UNIFORM,
                                                            render::ResourceType::UNIFORM },
                                                stencil_options.second };
    auto [vertex_data, vertex_indices] = model::getModelInfo("model/viking_room.obj");
    auto vertex_buffer = render::VertexBuffer{ std::span<const model::Vertex>{ vertex_data } };
    auto index_buffer = render::IndexBuffer{ vertex_indices };
    auto sampled_texture = render::SampledTexture{ "model/viking_room.png", true };

    auto view = trans::view::create(glm::vec3{ 5.0f, 5.0f, 5.0f });
    auto view_control = gui::CoDrawer{ control::cameraController(view) };
    auto proj = trans::proj::perspective({
      .width = 1920,
      .height = 1080,
    });

    auto model_data = trans::model::create();
    auto outline_data = model_data * trans::scale(glm::vec3{ 1.1f });
    auto uniforms = std::vector<render::Uniform<glm::mat4>>{};
    uniforms.reserve(4);
    uniforms.emplace_back(view);
    uniforms.emplace_back(proj);
    uniforms.emplace_back(model_data);
    uniforms.emplace_back(outline_data);
    auto draw_unit =
      render::DrawUnit{ pipeline_outline_1,
                        vertex_buffer,
                        index_buffer,
                        std::array<render::Resource*, 4>{
                          &uniforms[0], &uniforms[1], &uniforms[2], &sampled_texture },
                        std::nullopt };
    auto draw_unit_outline = render::DrawUnit{ pipeline_outline_2,
                                               vertex_buffer,
                                               index_buffer,
                                               std::array<render::Resource*, 3>{
                                                 &uniforms[0], &uniforms[1], &uniforms[3] },
                                               1 };
    // auto model_control = control::ModelInput{ model_datas[0] };
    auto pipeline_axis = render::Pipeline{ "axis.vert",
                                           "axis.frag",
                                           VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                                           axis::Vertex::getVertexInfo(),
                                           std::array{ render::ResourceType::UNIFORM,
                                                       render::ResourceType::UNIFORM,
                                                       render::ResourceType::UNIFORM },
                                           std::nullopt };
    auto axis_vertex_buffer = render::VertexBuffer::create<axis::Vertex>(axis::axis_model);
    auto axis_index_buffer =
      render::IndexBuffer{ views::iota(uint16_t(0), axis::axis_model.size()) |
                           ranges::to<std::vector>() };
    auto axis_draw_unit = render::DrawUnit{ pipeline_axis,
                                            axis_vertex_buffer,
                                            axis_index_buffer,
                                            std::array<render::Resource*, 3>{
                                              &uniforms[0], &uniforms[1], &uniforms[2] },
                                            std::nullopt };
    // auto controller = control::model::Controller{ model_datas[0] };
    // auto camera_controller = control::camera::Controller{ view, proj };
    // camera_controller.setInput();
    // controller.setInput();
    auto manager = mng::Manager{};
    loop.startLoop();
    drawer.waitDone();
  } catch (const std::exception& e) {

    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
