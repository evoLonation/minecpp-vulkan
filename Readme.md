## 环境配置
- 安装cmake（用于构建第三方库）
- 安装ninja
- 安装msys2
- 安装llvm，在msys2 mingw64中执行:
  - `pacman -Sy # 更新本地的包数据库(用于升级)`
  - `pacman -S mingw-w64-x86_64-clang-tools-extra`
  - `pacman -S mingw-w64-x86_64-clang`
  - `pacman -S mingw-w64-x86_64-libc++`
  - `pacman -S mingw-w64-x86_64-lldb`
- 目前llvm版本为20

# 第三方库
## glfw3
> GLFW是一个用C语言编写的库，专门针对OpenGL。GLFW为我们提供了将好东西呈现到屏幕上所需的基本必需品。它允许我们创建OpenGL上下文、定义窗口参数和处理用户输入，这对于我们的目的来说已经足够了。
### 获取方式 
https://github.com/glfw/glfw/releases/tag/3.3.9 （我这里下载win64版本）

### 需要的部分
- 头文件： include中的头文件
- 静态库： libglfw3dll.a 
- 动态库： glfw3.dll

## Vulkan
### 获取方式
https://vulkan.lunarg.com/sdk/home （下载VulkanSDK-1.3.268.0-Installer.exe）
### 需要的部分
- 头文件
- 静态库： vulkan-1.lib

## glm (version 1.0.1)
### 获取方式
https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip
### 构建方式
```
cmake -DBUILD_SHARED_LIBS=OFF -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DGLM_ENABLE_CXX_20=ON -DCMAKE_CXX_FLAGS="-Wno-unsafe-buffer-usage -Wno-used-but-marked-unused -Wno-nontrivial-memcall -Wno-format"  -B build -G "Ninja" .
cmake --build build -- all
```
### 需要的部分
- 头文件： glm目录
  - 删除glm.cppm和CMakeList.txt
- 静态库： libglm.a
- 模块接口文件： glm.cppm

## std_module
- 获取地址： C:\Users\ZhengyangZhao\msys64\mingw64\share\libc++\v1
- 放到third_party中试探性构建，并注释掉会报错的#include和using (修改处标注by zzy)

## stb_image
- 直接github搜，把stb_image.h放third_party即可

## imgui

github: https://github.com/ocornut/imgui
直接clone并checkout到docking分支即可
当前构建的commit id：87f12e56fe37411068309db7d8f978035c60060d
将源码clone到项目根目录后使用本构建系统将其编译为动态库(imgui.dll，注意静态链接时也要加上)
使用docking分支的代码以支持docking和viewport

resource.yml(include文件里需要包括imgui自身的头文件，vulkan，vk_video和GLFW):
```
source:
- imgui_demo.cpp
- imgui_draw.cpp
- imgui_tables.cpp
- imgui_widgets.cpp
- imgui_impl_glfw.cpp
- imgui_impl_vulkan.cpp
lib:
- libglfw3dll.a
- vulkan-1.lib
target:
- file: imgui.cpp
  name: imgui
  type: dll
include_dir:
- include
```

## assimp

github: https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.zip
### Windows
编译动态库的版本（默认情况即可）：
```shell
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -G "Ninja"
cmake --build .\build\
```
然后将动态库、静态库和include目录移至对应的目录下即可。
### MacOS
编译动态库版本：
```shell
# for windows
cmake -S . -B build -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -G "Ninja"
# for macos
cmake -S . -B build -G "Ninja"

cmake --build ./build
```
上述方法开启了MacOS X Framework方式来编译，编译产物在 build/bin/assimp.framework 中，unit和assimp都没有后缀名，
