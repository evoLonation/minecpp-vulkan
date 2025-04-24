import os
import subprocess
from pathlib import Path
import shutil
import sys
import textwrap
import zipfile
import urllib.request

import requests
from tqdm import tqdm
from tool import OutputMode, run_command

# 设置代理
os.environ["HTTP_PROXY"] = "http://localhost:7897"
os.environ["HTTPS_PROXY"] = "http://localhost:7897"

# 脚本路径
BASE_DIR = Path(os.getcwd())
print(f"BASE_DIR: {BASE_DIR}")
TP_DIR = BASE_DIR / "third_party"
INCLUDE_DIR = TP_DIR / "include"
DYN_LIB_DIR = TP_DIR / "dynamic_library"
STATIC_LIB_DIR = TP_DIR / "static_library"
MODULE_DIR = TP_DIR / "module"
TEMP_DIR = BASE_DIR / "temp"
VULKAN_DYLIB_FILE = "libvulkan.1.4.309.dylib"

dynamic_libs = []
dynamic_as_static_libs = []
static_libs = []
header_units = []
modules = []


def copy_dynamic_lib(src: Path, as_static: bool = False):
    print(f"copy_dynamic_lib: {src}")
    dst = DYN_LIB_DIR / src.name
    if dst.exists():
        dst.unlink()
    shutil.copy(src, dst)
    dynamic_libs.append(src.name)
    if as_static:
        dynamic_as_static_libs.append(src.name)


def copy_static_lib(src: Path):
    print(f"copy_static_lib: {src}")
    dst = STATIC_LIB_DIR / src.name
    if dst.exists():
        dst.unlink()
    shutil.copy(src, dst)
    static_libs.append(src.name)


def copy_include(src: Path, as_header_unit: bool = False):
    print(f"copy_include: {src}")
    dst = INCLUDE_DIR / src.name
    if dst.exists():
        dst.unlink() if src.is_file() else shutil.rmtree(dst)
    if src.is_dir():
        shutil.copytree(src, dst)
    else:
        shutil.copy(src, dst)
    if as_header_unit:
        assert src.is_file()
        header_units.append(src.name)


def copy_module_file(src: Path):
    print(f"copy_module_file: {src}")
    assert src.is_file()
    dst = MODULE_DIR / src.name
    if dst.exists():
        dst.unlink()
    shutil.copy(src, dst)
    modules.append(src.name)


def download_with_progress(url, filename):
    response = requests.get(url, stream=True)
    total = int(response.headers.get("content-length", 0))
    block_size = 1024  # 1 Kibibyte

    with open(filename, "wb") as file, tqdm(
        desc=f"Downloading {filename.name}",
        total=total,
        unit="iB",
        unit_scale=True,
        unit_divisor=1024,
    ) as bar:
        for data in response.iter_content(block_size):
            file.write(data)
            bar.update(len(data))


def build_vulkan():
    print("build vulkan...")
    # 假设已经包含VulkanSDK目录在项目目录中
    vulkan_dir = BASE_DIR / "VulkanSDK/1.4.309.0/macOS"
    dylib_path = vulkan_dir / "lib" / VULKAN_DYLIB_FILE
    copy_dynamic_lib(dylib_path, as_static=True)
    copy_include(vulkan_dir / "include/vulkan")
    copy_include(vulkan_dir / "include/vk_video")


def build_glfw():
    print("build glfw...")
    url = "https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.bin.MACOS.zip"
    zip_path = TEMP_DIR / "glfw.zip"
    download_with_progress(url, zip_path)
    with zipfile.ZipFile(zip_path, "r") as z:
        z.extractall(TEMP_DIR)
    glfw_dir = TEMP_DIR / "glfw-3.4.bin.MACOS"
    copy_dynamic_lib(glfw_dir / "lib-arm64/libglfw.3.dylib")
    copy_include(glfw_dir / "include/GLFW")


def build_glm():
    url = "https://github.com/g-truc/glm/archive/refs/tags/1.0.1.zip"
    zip_path = TEMP_DIR / "glm.zip"
    download_with_progress(url, zip_path)
    with zipfile.ZipFile(zip_path, "r") as z:
        z.extractall(TEMP_DIR)
    glm_dir = TEMP_DIR / "glm-1.0.1"
    run_command(
        f"cmake -DBUILD_SHARED_LIBS=OFF -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang "
        f"-DGLM_ENABLE_CXX_20=ON "
        f'-DCMAKE_CXX_FLAGS="-Wno-unsafe-buffer-usage -Wno-used-but-marked-unused '
        f'-Wno-nontrivial-memcall -Wno-format" '
        f"-B build -G Ninja .",
        output=OutputMode.NO_CAPTURE,
        cwd=glm_dir,
    )
    run_command("cmake --build build -- all", cwd=glm_dir, output=OutputMode.NO_CAPTURE)
    copy_static_lib(glm_dir / "build/glm/libglm.a")
    module = glm_dir / "glm/glm.cppm"
    # 在第一行加入 #define GLM_EXT_INLINE_NAMESPACE
    module_content = module.read_text()
    module_content = "#define GLM_EXT_INLINE_NAMESPACE\n" + module_content
    module.write_text(module_content)
    copy_module_file(module)
    module.unlink()
    copy_include(glm_dir / "glm")


def build_assimp():
    url = "https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.zip"
    zip_path = TEMP_DIR / "assimp.zip"
    download_with_progress(url, zip_path)
    with zipfile.ZipFile(zip_path, "r") as z:
        z.extractall(TEMP_DIR)
    assimp_dir = TEMP_DIR / "assimp-5.4.3"
    run_command(
        "cmake cmake -S . -B build -G Ninja",
        cwd=assimp_dir,
        output=OutputMode.NO_CAPTURE,
    )
    run_command("cmake --build build", cwd=assimp_dir, output=OutputMode.NO_CAPTURE)
    copy_dynamic_lib(assimp_dir / "build/bin/libassimp.5.4.3.dylib")
    assimp_include = assimp_dir / "include/assimp"
    copy_include(assimp_include)
    for p in (INCLUDE_DIR / "assimp").glob("*.in"):
        p.unlink()
    editorconfig = INCLUDE_DIR / "assimp/.editorconfig"
    if editorconfig.exists():
        editorconfig.unlink()
    shutil.copytree(
        assimp_dir / "build/include/assimp", INCLUDE_DIR / "assimp", dirs_exist_ok=True
    )


def build_stb_image():
    url = "https://raw.githubusercontent.com/nothings/stb/refs/heads/master/stb_image.h"
    path = INCLUDE_DIR / "stb_image.h"
    download_with_progress(url, path)


def build_imgui():
    commit_id = "87f12e56fe37411068309db7d8f978035c60060d"
    run_command(
        "git clone git@github.com:ocornut/imgui.git --branch docking "
        f"{commit_id} --single-branch --depth=1",
        cwd=TEMP_DIR,
        output=OutputMode.NO_CAPTURE,
    )
    imgui_dir = TEMP_DIR / commit_id
    for sub in [
        INCLUDE_DIR / "vulkan",
        INCLUDE_DIR / "vk_video",
        INCLUDE_DIR / "GLFW",
    ]:
        shutil.copytree(sub, imgui_dir / "include" / sub.name, dirs_exist_ok=True)
    for libname in ["libglfw.3.dylib", VULKAN_DYLIB_FILE]:
        shutil.copy(DYN_LIB_DIR / libname, imgui_dir / libname)
    resource_yml = imgui_dir / "resource.yml"
    resource_yml.write_text(
        textwrap.dedent(
            f"""\
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
        - { VULKAN_DYLIB_FILE }
        # need change install_name
        dylib: 
        - libglfw.3.dylib
        - { VULKAN_DYLIB_FILE }
        """
        )
    )
    run_command(
        f"python build_tools/build_ninja.py --root_dir {imgui_dir}",
        cwd=BASE_DIR,
        output=OutputMode.NO_CAPTURE,
    )
    run_command("ninja -C build", cwd=imgui_dir, output=OutputMode.NO_CAPTURE)
    copy_dynamic_lib(imgui_dir / "build/out/libimgui.dylib")
    imgui_include = INCLUDE_DIR / "imgui"
    imgui_include.mkdir(exist_ok=True)
    for f in [
        "imconfig.h",
        "imgui.h",
        "imgui_internal.h",
        "imstb_rectpack.h",
        "imstb_textedit.h",
        "imstb_truetype.h",
        "backends/imgui_impl_glfw.h",
        "backends/imgui_impl_vulkan.h",
    ]:
        shutil.copy(imgui_dir / f, imgui_include / Path(f).name)


def append_resource_files():
    def write(path: Path, content):
        path.write_text(content)

    write(
        TP_DIR / "resource.yml",
        textwrap.dedent(
            """\
        include_dir:
        - include
        sub_dir:
        - static_library
        - dynamic_library
        - module
        - include
        """
        ),
    )

    write(
        MODULE_DIR / "resource.yml",
        textwrap.dedent(
            """\
        module:
        - glm.cppm:
            provide: glm
        """
        ),
    )

    write(
        STATIC_LIB_DIR / "resource.yml",
        textwrap.dedent(
            """\
        lib:
        - libglm.a
        """
        ),
    )

    write(
        DYN_LIB_DIR / "resource.yml",
        textwrap.dedent(
            f"""\
        dylib:
        - libimgui.dylib
        - libassimp.5.4.3.dylib
        - { VULKAN_DYLIB_FILE }
        - libglfw.3.dylib
        lib:
        - libimgui.dylib
        - libassimp.5.4.3.dylib
        - { VULKAN_DYLIB_FILE }
        - libglfw.3.dylib
        """
        ),
    )

    write(
        INCLUDE_DIR / "resource.yml",
        textwrap.dedent(
            """\
        header_unit:
        - stb_image.h
        """
        ),
    )


# 主函数
def main():
    for d in [INCLUDE_DIR, DYN_LIB_DIR, STATIC_LIB_DIR, MODULE_DIR]:
        d.mkdir(parents=True, exist_ok=True)
    if TEMP_DIR.exists():
        shutil.rmtree(TEMP_DIR)
    TEMP_DIR.mkdir(parents=True)
    build_vulkan()
    build_glfw()
    build_glm()
    build_assimp()
    build_stb_image()
    build_imgui()
    append_resource_files()
    shutil.rmtree(TEMP_DIR)


if __name__ == "__main__":
    main()
