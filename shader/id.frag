#version 450

layout(location = 0) out uint id_color;

layout(set = 0, binding = 1) uniform IdBlock{uint data;} id;

void main() {
  id_color = id.data;
}