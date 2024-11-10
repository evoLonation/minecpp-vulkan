#version 450

layout(location = 0) in vec3 in_position;
// layout(location = 1) in vec3 in_color;
layout(location = 1) in dvec2 in_tex_coord;

// layout(location = 0) out vec3 frag_color;

layout(set = 0, binding = 0) uniform ModelBlock{mat4 data;} model;
layout(push_constant) uniform CameraData{mat4 view; mat4 proj;} camera;

void main() {
  gl_Position = camera.proj * camera.view * model.data * vec4(in_position, 1.0);
  // frag_color = in_color;
}