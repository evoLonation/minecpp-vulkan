## 环境配置
- 建议全程开启TUN mode
- 安装msys2
- 在msys2 mingw64中执行:
  - `pacman -S mingw-w64-x86_64-clang`
  - `pacman -S mingw-w64-x86_64-clang-tools-extra`
  
# 第三方库
## glfw3

[glfw/glfw: A multi-platform library for OpenGL, OpenGL ES, Vulkan, window and input (github.com)](https://github.com/glfw/glfw)

从github中下载release（我这里下载了win64）

> GLFW是一个用C语言编写的库，专门针对OpenGL。GLFW为我们提供了将好东西呈现到屏幕上所需的基本必需品。它允许我们创建OpenGL上下文、定义窗口参数和处理用户输入，这对于我们的目的来说已经足够了。

我们需要的是：

- include中的头文件， 将其移到项目目录的include目录下
- 对应编译环境的库文件（vs2022）
  - glfw3.lib

## Vulkan
在这个网站中下载vulkan的sdk并安装：
https://vulkan.lunarg.com/sdk/home 

- include目录：头文件
- Lib目录：静态库（需要使用其中的vulkan-1.lib）