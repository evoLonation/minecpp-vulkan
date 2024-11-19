#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_tex_coord;

layout(location = 0) out vec3 frag_pos;
layout(location = 1) out vec3 frag_normal;
layout(location = 2) out vec2 frag_tex_coord;

layout(set = 0, binding = 0) uniform ModelBlock{
  mat4 model; 
  mat3 normal_model;
} object;
layout(push_constant) uniform CameraData{mat4 view; mat4 proj;} camera;

void main() {
  vec4 frag_pos4 = object.model * vec4(in_position, 1.0);
  gl_Position = camera.proj * camera.view * frag_pos4;
  frag_pos = frag_pos4.xyz;
  frag_normal = object.normal_model * in_normal;
  frag_tex_coord = in_tex_coord;
}