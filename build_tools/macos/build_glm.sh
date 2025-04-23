# !/bin/zsh
set -e # Exit immediately if a command exits with a non-zero status.

# 下载GLM 1.0.1
curl -L https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip -o glm.zip 
unzip glm.zip
cd glm-1.0.1

# 构建
cmake -DBUILD_SHARED_LIBS=OFF \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_C_COMPILER=clang \
      -DGLM_ENABLE_CXX_20=ON \
      -DCMAKE_CXX_FLAGS="-Wno-unsafe-buffer-usage -Wno-used-but-marked-unused -Wno-nontrivial-memcall -Wno-format" \
      -B build -G "Ninja" .
cmake --build build -- all

mkdir -p ../third_party/static_library
mv build/glm/libglm.a ../third_party/static_library/
rm glm/CMakeLists.txt
mkdir -p ../third_party/module
# todo: add macro flags in glm.cppm
mv glm/glm.cppm ../third_party/module
mkdir -p ../third_party/include
mv glm ../third_party/include

# 清理
cd ..
rm -r glm-1.0.1
rm glm.zip