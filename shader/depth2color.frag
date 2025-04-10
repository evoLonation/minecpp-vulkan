#version 450

layout(location = 0) in vec2 tex_coord;

layout(set = 0, binding = 0) uniform sampler2D depth_sampler;

layout(location = 0) out vec4 out_color;


void main() {
  float depth = texture(depth_sampler, tex_coord).r;
  out_color = vec4(vec3(depth), 1.0);
}