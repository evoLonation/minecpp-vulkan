#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_tex_coord;

layout(location = 0) out vec4 out_color;

layout(set = 1, binding = 0) uniform sampler2D tex_sampler;

void main() {
  //out_color = vec4(frag_tex_coord, 0.0, 1.0);
  //out_color = texture(tex_sampler, frag_tex_coord);
  out_color = vec4(frag_color, 1.0) * texture(tex_sampler, frag_tex_coord);
}