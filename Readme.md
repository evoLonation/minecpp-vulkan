## 环境配置
- 安装cmake（用于构建第三方库）
- 安装msys2
- 在msys2 mingw64中执行:
  - `pacman -S mingw-w64-x86_64-clang`
  - `pacman -S mingw-w64-x86_64-libc++`
  - `pacman -S mingw-w64-x86_64-clang-tools-extra`

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

## glm
### 获取方式
https://github.com/g-truc/glm/releases/tag/1.0.0 (source code)
### 构建方式
```
cmake -DBUILD_SHARED_LIBS=OFF -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DGLM_ENABLE_CXX_20=ON -DCMAKE_CXX_FLAGS="-Wno-unsafe-buffer-usage -Wno-used-but-marked-unused"  -B build -G "Ninja" .
 cmake --build build -- all
```
### 需要的部分
- 头文件： glm目录
  - 删除glm.cppm和CMakeList.txt
- 静态库： libglm.a
- 模块接口文件： glm.cppm

## std_module
- 来自clang18版本的libc++
- 直接在libc++目录中用cmake构建得到std的模块接口文件
- 放到third_party中试探性构建，并注释掉会报错的#include和using
- todo：详细步骤