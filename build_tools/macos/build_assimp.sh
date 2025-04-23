# !/bin/zsh

set -e # Exit immediately if a command exits with a non-zero status.

curl -L https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.zip -o assimp.zip
unzip assimp.zip
cd assimp-5.4.3
cmake cmake -S . -B build -G "Ninja"
cmake --build build
mkdir -p ../third_party/dynamic_library
cp build/bin/libassimp.5.4.3.dylib ../third_party/dynamic_library/libassimp.5.dylib
mkdir -p ../third_party/include
cp -r include/assimp ../third_party/include/
rm ../third_party/include/assimp/.editorconfig
rm ../third_party/include/assimp/*.in
cp build/include/assimp/* ../third_party/include/assimp/
cd ..
rm -r assimp-5.4.3
rm assimp.zip