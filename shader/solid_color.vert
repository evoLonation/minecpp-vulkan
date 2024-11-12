#version 450

layout(location = 0) in vec3 in_position;

layout(set = 0, binding = 0) uniform ModelBlock{mat4 data;} model;
layout(push_constant) uniform CameraData{mat4 view; mat4 proj;} camera;

void main() {
  gl_Position = camera.proj * camera.view * model.data * vec4(in_position, 1.0);
}