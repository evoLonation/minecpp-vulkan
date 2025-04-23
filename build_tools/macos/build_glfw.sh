# !/bin/zsh

set -e # Exit immediately if a command exits with a non-zero status.

curl -L https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.bin.MACOS.zip -o glfw.zip
unzip glfw.zip
cd glfw-3.4.bin.MACOS/lib-arm64
mkdir -p ../../third_party/dynamic_library
cp libglfw.3.dylib ../../third_party/dynamic_library
cd ..
mkdir -p ../third_party/include
cp -r include/GLFW ../third_party/include/
cd ..
rm -r glfw-3.4.bin.MACOS
rm glfw.zip