import argparse
import os
import subprocess as sp
from tool import Platform, run_command, current_platform
from public import Compiler, DepCtx, Root

parser = argparse.ArgumentParser()
parser.add_argument("root_dir", type=str)
parser.add_argument("type", type=str, choices=["exe", "dll"], help="the target type")
parser.add_argument("input", type=str, help="the target source file")
parser.add_argument("output", type=str, help="the target file")
args = parser.parse_args()
Root.set_dir(args.root_dir)

from resources import load_resources


def get_dep_modules(file):
    with open(DepCtx.module_dep_file(file), "rt") as f:
        modules = [line.strip() for line in f.readlines()]
        # print(f"the dep modules of {file}: {modules}")
        return modules


resources = load_resources()

provide_module_map: dict[str, str] = {}
implement_module_map: dict[str, list[str]] = {}
for module in resources.modules:
    if module.implement != None:
        implement_module_map.setdefault(module.implement, []).append(module.file)
    else:
        provide_module_map[str(module.provide)] = module.file

# print(f"provide_module_map: {provide_module_map}")
# print(f"implement_module_map: {implement_module_map}")

obj_files = []

sources = [source for source in resources.sources if source.needed_by(args.input)]
obj_files.extend(Compiler.obj_file(source.file) for source in sources)

dep_modules_stack = list(
    set(module for source in sources for module in get_dep_modules(source.file))
)
found_modules: set[str] = set(dep_modules_stack)
while len(dep_modules_stack) > 0:
    # print(f"dep_modules: {dep_modules}")
    module = dep_modules_stack.pop()
    file = provide_module_map[module]
    obj_files.append(Compiler.obj_file(file))
    new_modules = set(get_dep_modules(file))
    if module in implement_module_map:
        for file in implement_module_map[module]:
            obj_files.append(Compiler.obj_file(file))
            new_modules |= set(get_dep_modules(file))
    new_modules -= found_modules
    found_modules |= new_modules
    dep_modules_stack.extend(new_modules)
# print("obj_files:")
# print("\n".join(obj_files))
# for obj_file in obj_files:
#     NinjaCtx.execute(NinjaCtx.get_file(NinjaCtx.Task.compile), obj_file)

link_files = [lib.file for lib in resources.lib_files]

run_command(
    Compiler.link(
        link_files=link_files,
        inputs=obj_files,
        output=args.output,
        shared=args.type == "dll",
    )
)


def get_linked_install_names(binary_path: str):
    output = run_command(["otool", "-L", str(binary_path)])
    lines = output.splitlines()[1:]  # 跳过第一行（是可执行文件名）
    if args.type == "dll":
        # 动态库的第一个是它自己, 忽略
        lines = lines[1:]
    return [line.strip().split(" ")[0] for line in lines]



def get_dylib_install_name(dylib_path: str):
    output = run_command(["otool", "-D", str(dylib_path)])
    return output.splitlines()[1].strip()


# 获取可执行文件中所有动态库 install_name
exe_install_names = set(get_linked_install_names(args.output))

# 遍历目录中的所有 .dylib
dylib_map = {}
dylib_install_names = set()
# print(resources.dylib_files)
for dylib in resources.dylib_files:
    # print(f"Checking: {dylib.file}")
    install_name = get_dylib_install_name(dylib.file)
    dylib_map[install_name] = dylib.file
    dylib_install_names.add(install_name)
# dylib_install_names 是 exe_install_names 的子集
assert dylib_install_names.issubset(
    exe_install_names
), f"exe_install_names: {exe_install_names}, dylib_install_names: {dylib_install_names}"

for old_name, dylib_file in dylib_map.items():
    new_name = f"@rpath/{os.path.basename(dylib_file)}"
    # print(f"Patching: {old_name} -> {new_name}")
    run_command(["install_name_tool", "-change", old_name, new_name, str(args.output)])
