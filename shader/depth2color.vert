#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_tex_coord;

layout(location = 0) out vec2 tex_coord;

void main() {
  gl_Position = vec4(in_position, 0.5, 1.0);
  tex_coord = in_tex_coord;
}