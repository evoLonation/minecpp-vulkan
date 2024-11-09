#version 450

layout(location = 0) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;
layout(location = 1) out uint id_color;

layout(set = 0, binding = 1) uniform sampler2D tex_sampler;
layout(set = 0, binding = 2) uniform IdBlock{uint data;} id;

void main() {
  out_color = texture(tex_sampler, frag_tex_coord);
  id_color = id.data;
}