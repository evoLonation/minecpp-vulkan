#version 450

layout(location = 0) out vec4 out_color;
layout(push_constant) uniform ColorBlock{
  layout(offset=128) vec3 data;
} color;

void main() {
  out_color = vec4(color.data, 1.0);
}