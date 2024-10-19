from enum import Enum
import inspect
from os import path
import os
import os.path as path
import subprocess as sp
from typing import overload
import ninja_syntax as ninja


class Workspace(Enum):
    build = "build"
    ninja = build
    gen = path.join(build, "gen")
    gen_shader = path.join(gen, "shader")
    gen_test = path.join(gen, "test")
    obj = path.join(build, "obj")
    pcm = path.join(build, "pcm")
    hpcm = path.join(build, "hpcm")
    out = path.join(build, "out")
    dep_scan = path.join(build, "dep_scan")
    cache = path.join(build, "cache")
    complete_dyndep = path.join(build, "complete_dyndep")


class PathCtx:
    root_dir = path.abspath("./")

    @staticmethod
    def set_root_dir(root_dir: str):
        PathCtx.root_dir = path.abspath(root_dir)

    @staticmethod
    def rel_root_path(file: str):
        return path.relpath(file, PathCtx.root_dir)

    @staticmethod
    def get_dir(workspace: Workspace):
        return path.join(PathCtx.root_dir, workspace.value)

    @staticmethod
    def mkdirs():
        for workspace in Workspace:
            os.makedirs(PathCtx.get_dir(workspace), exist_ok=True)


class NinjaCtx:
    class WriterContextManager(ninja.Writer):
        def __enter__(self):
            return self

        def __exit__(self, exc_type, exc_value, traceback):
            self.close()

    class Rule:
        precompile_header = "precompile_header"
        dep_scan = "dep_scan"
        precompile = "precompile"
        compile = "compile"
        link = "link"
        copy = "copy"
        shader_code = "shader_code_generate"
        shader_code_total = "shader_code_total_generate"
        complete_dyndep = "complete_dyndep"

    class Phony:
        header_unit = "header_unit"

        @staticmethod
        def target(name: str):
            return f"target/{name}"

        @staticmethod
        def complete_dep_module(name: str):
            return f"module/{name}"

        @staticmethod
        def complete_dep_source(file: str):
            return path.join("source", PathCtx.rel_root_path(file))

    class Task(Enum):
        total = "build.ninja"
        compile = "compile.ninja"
        header_precompile = "header_precompile.ninja"
        dep_scan = "dep_scan.ninja"
        complete_dep = "complete_dep.ninja"
        target = "target.ninja"
        shader_gen = "shader_gen.ninja"
        test_gen = "test_gen.ninja"

    @staticmethod
    def get_file(task: Task):
        return path.join(PathCtx.get_dir(Workspace.ninja), task.value)

    @staticmethod
    def execute(ninja_file: str, extra="", stdout=None, check=True):
        return sp.run(
            f"ninja -C {path.dirname(ninja_file)} -f {path.basename(ninja_file)} {extra}",
            stdout=stdout,
            check=check,
        )

    @staticmethod
    def open_ninja(task_or_file: Task | str):
        if isinstance(task_or_file, NinjaCtx.Task):
            file = NinjaCtx.get_file(task_or_file)
        else:
            file = task_or_file
        return NinjaCtx.WriterContextManager(open(file, "wt"))


class Script(Enum):
    dep_scan = "dep_scan.py"
    link = "link.py"
    shader_gen = "shader_gen.py"
    test_gen = "test_gen.py"
    complete_dyndep = "complete_dyndep.py"

    @staticmethod
    def get_command(script: "Script", args: list[str]) -> str:
        abspath = path.join(path.dirname(path.abspath(__file__)), script.value)
        command = ["python", abspath] + args
        return sp.list2cmdline(command)


class Compiler:
    clang_executable_path = "clang"
    system_include_dirs = [
        "C:/Users/ZhengyangZhao/msys64/mingw64/include/c++/v1",
        "C:/Users/ZhengyangZhao/msys64/mingw64/lib/clang/18/include",
    ]
    system_link_dirs = [
        "C:/Users/18389/msys2/mingw64/lib",
    ]
    system_link_libs = [
        "c++",
    ]

    current_flag = [
        clang_executable_path,
        "-std=c++23",
        "-fexperimental-library",
        "-nostdinc++",
        "-nostdlib++",
        "-Wno-unused-command-line-argument",
        # for a deprecation bug occured in clang18 with std module:
        # https://github.com/llvm/llvm-project/issues/75057
        "-Wno-deprecated-declarations",
        "-Wno-experimental-header-units",
        "-g",
    ]

    @staticmethod
    def precompile(
        include_dirs: list[str], config: str | None, input: str, output: str
    ):
        return sp.list2cmdline(
            Compiler.current_flag
            + ([] if config is None else ["--config", config])
            + ["-fprebuilt-module-path=" + PathCtx.get_dir(Workspace.pcm)]
            + ["-isystem" + x for x in Compiler.system_include_dirs]
            + ["-I" + x for x in include_dirs]
            + ["--precompile", input, "-o", output]
        )

    @staticmethod
    def compile(include_dirs: list[str], config: str | None, input: str, output: str):
        return sp.list2cmdline(
            Compiler.current_flag
            + ([] if config is None else ["--config", config])
            + ["-fprebuilt-module-path=" + PathCtx.get_dir(Workspace.pcm)]
            + ["-isystem" + x for x in Compiler.system_include_dirs]
            + ["-I" + x for x in include_dirs]
            + ["-c", input, "-o", output]
        )

    @staticmethod
    def header_precompile(include_dirs: list[str], input: str, output: str):
        return sp.list2cmdline(
            Compiler.current_flag
            + ["-I" + x for x in include_dirs]
            + ["-fmodule-header", "-xc++-header"]
            + [input, "-o", output]
        )

    @staticmethod
    def link(link_files: list[str], inputs: list[str], output: str):
        link_dirs = list(set([path.dirname(file) for file in link_files]))
        link_libs = []
        for file in link_files:
            filename = path.basename(file)
            if filename.startswith("lib") and filename.endswith(".a"):
                link_libs.append(filename[3:-2])
            elif filename.endswith(".lib") or filename.endswith(".dll"):
                link_libs.append(filename[:-4])
        return (
            Compiler.current_flag
            + inputs
            + ["-L" + dir for dir in link_dirs + Compiler.system_link_dirs]
            + ["-l" + lib for lib in link_libs + Compiler.system_link_libs]
            + ["-o", output]
        )

    @staticmethod
    def obj_file(file: str):
        return path.join(
            PathCtx.get_dir(Workspace.obj), PathCtx.rel_root_path(file) + ".o"
        )

    @staticmethod
    def pcm_file(module: str):
        return path.join(
            PathCtx.get_dir(Workspace.pcm), module.replace(":", "-") + ".pcm"
        )

    @staticmethod
    def header_pcm_file(file: str):
        return path.join(PathCtx.get_dir(Workspace.hpcm), path.basename(file) + ".pcm")

    @staticmethod
    def target_file(target: str):
        return path.join(PathCtx.get_dir(Workspace.out), target + ".exe")

    @staticmethod
    def dynamic_dir(file: str):
        return path.join(PathCtx.get_dir(Workspace.out), path.basename(file))


class DepCtx:
    @staticmethod
    def dyndep_file(file: str):
        return path.join(
            PathCtx.get_dir(Workspace.dep_scan),
            PathCtx.rel_root_path(file) + ".dd",
        )

    @staticmethod
    def header_dep_config_file(file: str):
        return path.join(
            PathCtx.get_dir(Workspace.dep_scan),
            PathCtx.rel_root_path(file) + ".cfg",
        )

    @staticmethod
    def module_dep_file(file: str):
        return path.join(
            PathCtx.get_dir(Workspace.dep_scan),
            PathCtx.rel_root_path(file) + ".deps",
        )

    @staticmethod
    def complete_dyndep_module_file(module: str):
        return path.join(
            PathCtx.get_dir(Workspace.complete_dyndep), "module", f"{module}.dd"
        )

    @staticmethod
    def complete_dyndep_source_file(file: str):
        return path.join(
            PathCtx.get_dir(Workspace.complete_dyndep),
            "source",
            PathCtx.rel_root_path(f"{file}.dd"),
        )
