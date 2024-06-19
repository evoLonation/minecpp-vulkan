#version 450

layout(location = 0) in vec3 in_position;
// layout(location = 1) in vec3 in_color;
layout(location = 1) in dvec2 in_tex_coord;

// layout(location = 0) out vec3 frag_color;
layout(location = 0) out vec2 frag_tex_coord;

layout(set = 0, binding = 0) uniform ModelBlock{mat4 data;} model;
layout(set = 1, binding = 0) uniform ViewBlock{mat4 data;} view;
layout(set = 1, binding = 1) uniform ProjBlock{mat4 data;} proj;

void main() {
  gl_Position = proj.data * view.data * model.data * vec4(in_position, 1.0);
  // frag_color = in_color;
  frag_tex_coord = vec2(in_tex_coord);
}