#version 450

layout(location = 0) out vec4 out_color;
layout(set = 0, binding = 1) uniform ColorBlock{
  vec3 data;
} color;

void main() {
  out_color = vec4(color.data, 1.0);
}