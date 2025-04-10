#version 450

layout(location = 0) in vec3 in_position;

layout(set = 0, binding = 0) uniform ModelBlock{
  mat4 model; 
} object;

layout(push_constant) uniform LightData{mat4 view; mat4 proj;} light;

void main() {
  gl_Position = light.proj * light.view * object.model * vec4(in_position, 1.0);
}