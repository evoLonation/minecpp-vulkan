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

if current_platform == Platform.MACOS:
    for file in link_files:
        if file.endswith(".dylib"):
            run_command(
                ["install_name_tool", "-id", f"@rpath/{os.path.basename(file)}", file]
            )

run_command(
    Compiler.link(
        link_files=link_files,
        inputs=obj_files,
        output=args.output,
        shared=args.type == "dll",
    )
)
