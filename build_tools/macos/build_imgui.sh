# !/bin/zsh
set -e # Exit immediately if a command exits with a non-zero status.
git clone git@github.com:ocornut/imgui.git --branch docking 87f12e56fe37411068309db7d8f978035c60060d --single-branch --depth=1
cd 87f12e56fe37411068309db7d8f978035c60060d
mkdir -p include
cp -r ../third_party/include/vk_video include
cp -r ../third_party/include/vulkan include
cp -r ../third_party/include/GLFW include
cp ../third_party/dynamic_library/libglfw.3.dylib .
cp ../third_party/static_library/libvulkan.1.dylib . 
cat >> resource.yml <<EOF
source:
- imgui_demo.cpp
- imgui_draw.cpp
- imgui_tables.cpp
- imgui_widgets.cpp
- backends/imgui_impl_glfw.cpp
- backends/imgui_impl_vulkan.cpp
target:
- file: imgui.cpp
  name: imgui
  type: dll
include_dir:
- .
- include
- backends
lib:
- libglfw.3.dylib
- libvulkan.1.dylib
EOF
python ../build_tools/build_ninja.py
ninja -C build
cp build/out/libimgui.dylib ../third_party/dynamic_library/
cp build/out/libimgui.dylib ../third_party/static_library/
mkdir -p ../third_party/include/imgui
cp imconfig.h ../third_party/include/imgui/
cp imgui.h ../third_party/include/imgui/
cp imgui_internal.h ../third_party/include/imgui/
cp imstb_rectpack.h ../third_party/include/imgui/
cp imstb_textedit.h ../third_party/include/imgui/
cp imstb_truetype.h ../third_party/include/imgui/
cp backends/imgui_impl_glfw.h ../third_party/include/imgui/
cp backends/imgui_impl_vulkan.h ../third_party/include/imgui/

# clean up
cd ..
rm -r 87f12e56fe37411068309db7d8f978035c60060d