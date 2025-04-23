# !/bin/zsh

set -e # Exit immediately if a command exits with a non-zero status.

export HTTP_PROXY=http://localhost:7897
export HTTPS_PROXY=http://localhost:7897

./build_tools/macos/build_glfw.sh
./build_tools/macos/build_glm.sh
./build_tools/macos/build_assimp.sh
./build_tools/macos/build_imgui.sh
./build_tools/macos/build_stb_image.sh

cat >> third_party/resource.yml << EOF
include_dir:
- include
sub_dir:
- static_library
- dynamic_library
- module
- include
EOF

cat >> third_party/module/resource.yml << EOF
module:
- glm.cppm:
    provide: glm
- std/std.cppm:
    provide: std
EOF

cat >> third_party/static_library/resource.yml << EOF
lib:
- libglm.a
EOF

cat >> third_party/dynamic_library/resource.yml << EOF
dylib:
- libimgui.dylib
- libassimp.5.dylib
- libvulkan.1.dylib
- libglfw.3.dylib
lib:
- libimgui.dylib
- libassimp.5.dylib
- libvulkan.1.dylib
- libglfw.3.dylib
EOF

cat >> third_party/include/resource.yml << EOF
header_unit:
- stb_image.h
EOF