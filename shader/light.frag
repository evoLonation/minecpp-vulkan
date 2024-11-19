#version 450

// 从顶点着色器传来的顶点坐标（世界空间）
layout(location = 0) in vec3 pos;
// 从顶点着色器传来的法向量（世界空间）
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 tex_coord;

// 物体本身的颜色
layout(set = 0, binding = 1) uniform MaterialInfo{
  float shininess;
} material;
layout(set = 0, binding = 2) uniform sampler2D diffuse_sampler;
layout(set = 0, binding = 3) uniform sampler2D specular_sampler;

// 光源的颜色
layout(set = 1, binding = 0) uniform LightInfo {
  vec3 direction;
  vec3 ambient;
  vec3 diffuse;
  vec3 specular;
  vec3 view_pos;
} light;

layout(location = 0) out vec4 out_color;

void main()
{
  vec3 material_diffuse = vec3(texture(diffuse_sampler, tex_coord));
  vec3 material_specular = vec3(texture(specular_sampler, tex_coord));
  // ambient lighting
  vec3 ambient =  light.ambient * material_diffuse;
  // diffuse lighting
  vec3 norm = normalize(normal);
  // 通过计算片段的法向量与光线方向的点积
  vec3 diffuse = max(dot(norm, -light.direction), 0.0f) * material_diffuse * light.diffuse;
  // specular lighting, 镜面反射光，是光从顶点反射过来的方向与顶点到摄像机方向的点积
  vec3 reflect_dir = reflect(light.direction, norm);
  vec3 view_dir = normalize(light.view_pos - pos);
  // pow 用于计算 x 的 y 次方
  // 因此后面的数字越大，亮点越集中
  vec3 specular = pow(max(dot(view_dir, reflect_dir), 0.0), material.shininess) * material_specular * light.specular;
  vec3 result = ambient + diffuse + specular;
  out_color = vec4(result, 1.0);
}