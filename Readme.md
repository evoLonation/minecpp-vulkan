# minecpp-vulkan

一个用 Vulkan 写的小型引擎项目。

项目围绕几类比较底层的图形工程问题展开，最后形成了这样一套东西：

- C++20 modules
- `resource.yml + Python + Ninja` 构建系统
- Vulkan 资源封装
- 自动同步 / 命令提交抽象
- 场景树、资产系统、模型导入
- 一个能工作的编辑器原型
- 代码行数37,043行，可随时基于渲染框架填充新的材质和着色器


## 入口

程序入口基本就是把上下文、渲染循环、相机和编辑器面板挂起来：

```cpp
int main() {
  try {
    auto ctx = ctx::Context{ "hello vulkan", 1280, 720 };
    auto render_pass = eg::RenderPassLoop{};

    auto camera = eg::Camera{};
    auto controller = eg::CameraController{ &camera };
    auto panel = eg::TransformControllerPanel{};
    auto scene_panel = eg::ScenePanel{};

    fw::Loop::getInstance().run();
  } catch (const std::exception& e) {
    std::print("catch exception at root:\n{}\n", e.what());
    return 1;
  }
  return 0;
}
```

这里面几层职责是分开的：

- `framework/` 负责上下文、输入、主循环
- `render/` 负责 Vulkan 基础设施
- `engine/` 负责场景、物件、资产、编辑器和具体 pipeline

如果从代码开始看，建议顺着这个顺序：

1. [`main.cc`](/Users/zhaozhengyang/minecpp-vulkan/main.cc)
2. [`framework/context.cc`](/Users/zhaozhengyang/minecpp-vulkan/framework/context.cc)
3. [`framework/loop.ccm`](/Users/zhaozhengyang/minecpp-vulkan/framework/loop.ccm)
4. [`engine/pipelines/loop.ccm`](/Users/zhaozhengyang/minecpp-vulkan/engine/pipelines/loop.ccm)

## 构建系统

这个项目没有直接交给一套现成的 CMake 组织，核心原因是需要围绕 C++20 modules 自己控制工程描述和依赖生成流程。

工程入口是 [`resource.yml`](/Users/zhaozhengyang/minecpp-vulkan/resource.yml)：

```yaml
include_dir:
- include
- toy
- engine
module:
- glfw.ccm:
    provide: glfw
- transform.ccm:
    provide: math
- std/std.cppm:
    provide: std
sub_dir:
- include
- third_party
- toy
- render
- shader
- framework
- engine
- gui
target:
- main.cc:
    name: hello_vulkan
    type: executable
test:
- transform_test.cc
```

`build_tools/` 负责做这些事：

- 递归收集模块、源码、测试和目标
- 扫描 module 依赖
- 生成 Ninja
- 生成 shader 对应的模块代码
- 生成测试入口
- 维护 `compile_commands.json`

入口文件：

- [`build_tools/build_ninja.py`](/Users/zhaozhengyang/minecpp-vulkan/build_tools/build_ninja.py)
- [`build_tools/resources.py`](/Users/zhaozhengyang/minecpp-vulkan/build_tools/resources.py)
- [`build_tools/public.py`](/Users/zhaozhengyang/minecpp-vulkan/build_tools/public.py)

## 同步和提交

这部分是整个项目里最重的一块。

项目没有把 barrier、layout transition 和 queue family transfer 散落在上层逻辑里，而是做了两代抽象：

- `tracker + submitter + executor`
- `tracker2 + execution + submitter2`

第一代还在主路径里，第二代是后面开始重构的方向。

当前渲染循环里有一段典型调用，从离屏颜色图 copy 到 swapchain：

```cpp
auto submitter = rd::Submitter{ rd::FamilyType::TRANSFER };
submitter.addNeedSync(
  &_color.getTracker(),
  { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT },
  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
);
submitter.addNeedSync(
  &ctx.image_manager->getTracker(),
  { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT },
  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
);
submitter.submit([&](VkCommandBuffer cmdbuf) {
  rd::copyImageToImage(
    cmdbuf,
    _color.getImage(),
    ctx.image_manager->getImage(),
    VK_IMAGE_ASPECT_COLOR_BIT,
    { 0, 0 },
    { 0, 0 },
    _color.getExtent()
  );
});
```

这里保留的是一种比较明确的调用方式：使用方声明“如何使用资源”，同步细节尽量收在 tracker 和 submitter 那层。

入口文件：

- [`render/command/tracker.ccm`](/Users/zhaozhengyang/minecpp-vulkan/render/command/tracker.ccm)
- [`render/command/submitter.ccm`](/Users/zhaozhengyang/minecpp-vulkan/render/command/submitter.ccm)
- [`render/command/exec/submitter2.ccm`](/Users/zhaozhengyang/minecpp-vulkan/render/command/exec/submitter2.ccm)
- [`render/command/test.cc`](/Users/zhaozhengyang/minecpp-vulkan/render/command/test.cc)

## 场景和资产

渲染层之上又搭了几层对象模型：

- `Node`
- `SceneObject`
- `DrawUnit`
- `Mesh / Texture / Camera`
- `Package / Asset / Guid`

`engine/core/package.ccm` 里是一套自定义的资产持久化系统，底层还做了 `LinkedFile` 这种分页文件结构。

模型导入走 Assimp，导入之后会挂到节点树里，再接到资产系统。

ScenePanel 里有一段代码能说明这个路径：

```cpp
if (ImGui::Button("Create Scene By Model File")) {
  node = std::make_shared<Node>();
  eg::DoTheImportThing(model_path.data())->attachTo(*node);
}

if (node) {
  node->setOwnedPackage(_package.get());
  auto scene_node = SceneNode{ std::move(node), scene_name.data() };
  scene_node.setAssetName(scene_name.data());
  scene_node.setOwnedPackage(_package.get());
  _loaded_scenes[scene_name.data()] = std::move(scene_node);
}
```

相关代码：

- [`engine/core/node.ccm`](/Users/zhaozhengyang/minecpp-vulkan/engine/core/node.ccm)
- [`engine/core/object.ccm`](/Users/zhaozhengyang/minecpp-vulkan/engine/core/object.ccm)
- [`engine/core/package.ccm`](/Users/zhaozhengyang/minecpp-vulkan/engine/core/package.ccm)
- [`engine/core/importer.ccm`](/Users/zhaozhengyang/minecpp-vulkan/engine/core/importer.ccm)

## 编辑器

编辑器部分很朴素，基于 ImGui。

现在有两块主要东西：

- `ScenePanel`
- `TransformControllerPanel`

能做的事情包括：

- 创建空场景
- 用基础几何体创建场景
- 导入模型文件
- 保存和加载 scene
- 查看 scene 树
- 选中对象后做平移、旋转、缩放

相关代码：

- [`engine/editor/scene/gui.ccm`](/Users/zhaozhengyang/minecpp-vulkan/engine/editor/scene/gui.ccm)
- [`engine/editor/scene/scene.ccm`](/Users/zhaozhengyang/minecpp-vulkan/engine/editor/scene/scene.ccm)
- [`engine/editor/transform/transform.ccm`](/Users/zhaozhengyang/minecpp-vulkan/engine/editor/transform/transform.ccm)

## 目录

```text
.
├── build_tools/   # 构建系统
├── engine/        # 场景、资产、导入器、编辑器、具体 pipeline
├── framework/     # 上下文、输入、主循环
├── gui/           # ImGui 上下文和小工具
├── render/        # Vulkan 资源、同步、命令执行、present、pipeline 基础设施
├── shader/        # GLSL
├── toy/           # 基础设施
└── docs/          # 技术报告和速记稿
```

## 依赖

主要依赖：

- Vulkan SDK
- GLFW
- GLM
- ImGui docking 分支
- Assimp
- stb_image

- Clang / LLVM
- Vulkan SDK
- Python 构建脚本依赖
- ImGui / GLFW / Assimp / GLM 的本地目录布局
