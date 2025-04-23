from contextlib import contextmanager
from enum import Enum, auto
import inspect
from os import path
import os
import os.path as path
import re
import subprocess as sp
from typing import overload
import uuid
import ninja_syntax as ninja
from tool import (
    Platform,
    current_platform,
    run_command,
)


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

    def get_dir(self):
        return path.join(Root.dir, self.value)

    @staticmethod
    def mkdirs():
        for workspace in Workspace:
            os.makedirs(workspace.get_dir(), exist_ok=True)


class Root:
    # add norm case to norm standardize the capitalization of paths from __file__
    dir = path.normcase(path.abspath("./"))

    @staticmethod
    def set_dir(root_dir: str):
        # add norm case to norm standardize the capitalization of paths from __file__
        Root.dir = path.normcase(path.abspath(root_dir))

    @staticmethod
    def relpath(file: str):
        return path.relpath(file, Root.dir)


@contextmanager
def open_ninja(file: str):
    writer = ninja.Writer(open(file, "wt"))
    yield writer
    writer.close()


class NinjaFile:
    task = "build"
    work_dir: str | None = None

    @classmethod
    def get_file(cls):
        return cls.task + ".ninja"

    @classmethod
    def get_path(cls):
        if cls.work_dir is None:
            return path.join(Workspace.ninja.get_dir(), cls.get_file())
        else:
            return path.join(cls.work_dir, cls.get_file())

    @classmethod
    def open(cls):
        return open_ninja(cls.get_path())

    @classmethod
    def execute(cls, extra: str = "") -> str:
        command = cls.get_command(extra)
        return run_command(command)

    @classmethod
    def get_command(cls, extra: str = ""):
        file = cls.get_path()
        command = f"ninja -C {path.dirname(file)} -f {path.basename(file)} {extra}"
        return command

    @classmethod
    def dry_run(cls, targets: str):
        result = cls.execute(f"{targets} -n")
        lines = [x.strip() for x in result.split("\n")]
        if lines[-1] == "":
            lines = lines[:-1]
        assert lines[0].startswith("ninja: Entering directory")
        if lines[1].startswith("ninja: no work to do."):
            return []
        pattern = re.compile(r"\[(\d+)/(\d+)\]")
        outputs = []
        target_n = None
        for i, line in enumerate(lines[1:]):
            match = re.search(pattern, line)
            assert match is not None, f"line: {line}"
            assert match.start() == 0
            if target_n is None:
                target_n = int(match.group(2))
            else:
                assert target_n == int(match.group(2))
            assert int(match.group(1)) == i + 1
            outputs.append(line[match.end() :])
        assert target_n == len(outputs)
        return outputs


class HeaderNinja(NinjaFile):
    task = "header"

    class Rule:
        precompile = "precompile"

    class Phony:
        header_unit = "header_unit"


class DepScanNinja(NinjaFile):
    task = "dep_scan"

    class Rule:
        dep_scan = "dep_scan"

    class Phony:
        dep_scan = "dep_scan"


class CompileNinja(NinjaFile):
    task = "compile"

    class Rule:
        precompile = "precompile"
        compile_pcm = "compile_pcm"
        compile = "compile"

    class Phony:
        pcm = "pcm"


class CompleteDepNinja(NinjaFile):
    task = "complete_dep"

    class Rule:
        complete_dyndep = "complete_dyndep"

    class Phony:
        @staticmethod
        def module(name: str):
            return f"module/{name}"

        @staticmethod
        def source(file: str):
            return path.join("source", Root.relpath(file))


class TargetNinja(NinjaFile):
    task = "target"

    class Rule:
        link = "link"
        copy = "copy"

    class Phony:
        @staticmethod
        def target(name: str):
            return f"target/{name}"


class RemoveInvalidNinja(NinjaFile):
    task = "remove_invalid"

    class Rule:
        remove_invalid = "remove_invalid"

    @staticmethod
    def get_output():
        return path.join(Workspace.build.get_dir(), "remove_invalid")


class ShaderGenNinja(NinjaFile):
    task = "shader_gen"

    class Rule:
        shader_code = "shader_code_generate"
        shader_code_total = "shader_code_total_generate"


class TestGenNinja(NinjaFile):
    task = "test_gen"

    class Rule:
        test_main = "test_main_generate"


class Script(Enum):
    dep_scan = "dep_scan.py"
    link = "link.py"
    shader_gen = "shader_gen.py"
    test_gen = "test_gen.py"
    complete_dyndep = "complete_dyndep.py"
    clangd_remove_invaid = "clangd_remove_invalid.py"

    @staticmethod
    def get_command(script: "Script", args: list[str]) -> str:
        # add norm case to norm standardize the capitalization of paths from __file__
        abspath = path.join(
            path.dirname(path.normcase(path.abspath(__file__))), script.value
        )
        command = ["python", abspath] + args
        return sp.list2cmdline(command)


class Compiler:
    clang_executable_path = "clang++"

    # only use for clang-scan-deps because clang-scan-deps unable to find system headers in p1689 format
    # more information: https://github.com/llvm/llvm-project/issues/75057
    # you can use `clang++ -v -E - < dev/null` to find the system include dirs
    system_include_dirs = [
        "/opt/homebrew/Cellar/llvm/20.1.2/include/c++/v1",
        "/opt/homebrew/Cellar/llvm/20.1.2/lib/clang/20/include",
        "/Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk/usr/include",
    ]
    system_framework_dir = "/Library/Developer/CommandLineTools/SDKs/MacOSX15.sdk/System/Library/Frameworks"

    # only use when macos
    system_link_frameworks = ["Cocoa", "IOKit"]

    base_flag = [
        clang_executable_path,
        "-std=c++23",
        "-fexperimental-library",
        # todo: delete unused flags
        "-Wno-unused-command-line-argument",
        # for a deprecation bug occured in clang18 with std module:
        # https://github.com/llvm/llvm-project/issues/75057
        "-Wno-deprecated-declarations",
        # suppress error from clangd, must here, can not append in back
        "-fretain-comments-from-system-headers",
        "-Wno-experimental-header-units",
        "-g",
    ]

    @staticmethod
    def __get_platform_macro() -> str:
        if current_platform == Platform.WINDOWS:
            macro = "PLATFORM_WINDOWS"
        elif current_platform == Platform.MACOS:
            macro = "PLATFORM_MACOS"
        else:
            raise RuntimeError(f"Unsupported platform: {current_platform}")
        return macro

    @staticmethod
    def precompile(
        include_dirs: list[str], config: str | None, input: str, output: str
    ):
        return sp.list2cmdline(
            Compiler.base_flag
            + ([] if config is None else ["--config", config])
            + ["-D" + Compiler.__get_platform_macro()]
            + ["-fprebuilt-module-path=" + Workspace.pcm.get_dir()]
            + ["-I" + x for x in include_dirs]
            + ["--precompile", input, "-o", output]
        )

    @staticmethod
    def compile(
        include_dirs: list[str],
        config: str | None,
        input: str,
        output: str,
        extra: str = "",
    ):
        return sp.list2cmdline(
            Compiler.base_flag
            + ([] if config is None else ["--config", config])
            + ([extra] if extra else [])
            + ["-D" + Compiler.__get_platform_macro()]
            + ["-fprebuilt-module-path=" + Workspace.pcm.get_dir()]
            + ["-I" + x for x in include_dirs]
            + ["-c", input, "-o", output]
        )

    @staticmethod
    def scan_deps(include_dirs: list[str], input: str):
        return "clang-scan-deps -format=p1689 -- " + sp.list2cmdline(
            Compiler.base_flag
            + ["-fprebuilt-module-path=" + Workspace.pcm.get_dir()]
            + ["-isystem" + x for x in Compiler.system_include_dirs]
            + ["-F" + Compiler.system_framework_dir]
            + ["-I" + x for x in include_dirs]
            + [input]
        )

    @staticmethod
    def get_macro_flag(macros: list[tuple[str, str]]):
        return [f"-D{x}={y}" for x, y in macros]

    @staticmethod
    def header_precompile(include_dirs: list[str], input: str, output: str):
        return sp.list2cmdline(
            Compiler.base_flag
            + ["-I" + x for x in include_dirs]
            + ["-fmodule-header", "-xc++-header"]
            + [input, "-o", output]
        )

    @staticmethod
    def link(link_files: list[str], inputs: list[str], output: str, shared: bool):
        link_dirs = list(set([path.dirname(file) for file in link_files]))
        link_libs = []
        for file in link_files:
            filename = path.basename(file)
            if filename.startswith("lib") and filename.endswith(".a"):
                link_libs.append(filename[3:-2])
            elif filename.endswith(".lib") or filename.endswith(".dll"):
                link_libs.append(filename[:-4])
            elif filename.startswith("lib") and filename.endswith(".dylib"):
                link_libs.append(filename[3:-6])
            else:
                link_libs.append(filename)
        return (
            Compiler.base_flag
            + inputs
            + (["-shared"] if shared else [])
            + ["-L" + dir for dir in link_dirs]
            + ["-l" + lib for lib in link_libs]
            + [
                x
                for pair in [
                    ["-framework", x]
                    for x in Compiler.system_link_frameworks
                    if current_platform == Platform.MACOS
                ]
                for x in pair
            ]
            + (
                ["-Wl,-rpath,@executable_path"]
                if not shared and current_platform == Platform.MACOS
                else []
            )
            + ["-o", output]
            # macos -install_name 设置的是：其他程序链接这个库时记住的运行时路径
            + (
                ["-install_name", f"@rpath/{path.basename(output)}"]
                if shared and current_platform == Platform.MACOS
                else []
            )
        )

    @staticmethod
    def hpcm_flag(hpcm_files: list[str], in_config: bool = False) -> str:
        if in_config:
            return "\n".join(
                ["-fmodule-file=" + x.replace("\\", "\\\\") for x in hpcm_files]
            )
        else:
            return sp.list2cmdline(["-fmodule-file=" + x for x in hpcm_files])

    @staticmethod
    def obj_file(file: str):
        return path.join(Workspace.obj.get_dir(), Root.relpath(file) + ".o")

    @staticmethod
    def pcm_file(module: str):
        return path.join(Workspace.pcm.get_dir(), module.replace(":", "-") + ".pcm")

    @staticmethod
    def hpcm_file(file: str):
        return path.join(Workspace.hpcm.get_dir(), path.basename(file) + ".pcm")

    @staticmethod
    def executable_file(target: str):
        suffix = ".exe" if current_platform == Platform.WINDOWS else ""
        return path.join(Workspace.out.get_dir(), target + suffix)

    @staticmethod
    def dll_file(target: str):
        if current_platform == Platform.WINDOWS:
            prefix = ""
            suffix = ".dll"
        elif current_platform == Platform.MACOS:
            prefix = "lib"
            suffix = ".dylib"
        else:
            raise RuntimeError("Unsupported platform")
        return path.join(Workspace.out.get_dir(), prefix + target + suffix)

    @staticmethod
    def dynamic_dest(file: str):
        return path.join(Workspace.out.get_dir(), path.basename(file))


class DepCtx:
    @staticmethod
    def dyndep_file(file: str):
        return path.join(Workspace.dep_scan.get_dir(), Root.relpath(file) + ".dd")

    @staticmethod
    def header_dep_config_file(file: str):
        return path.join(Workspace.dep_scan.get_dir(), Root.relpath(file) + ".cfg")

    @staticmethod
    def module_dep_file(file: str):
        return path.join(Workspace.dep_scan.get_dir(), Root.relpath(file) + ".deps")

    @staticmethod
    def complete_dyndep_module_file(module: str):
        return path.join(
            Workspace.complete_dyndep.get_dir(),
            "module",
            f"{module.replace(':', '-')}.dd",
        )

    @staticmethod
    def complete_dyndep_source_file(file: str):
        return path.join(
            Workspace.complete_dyndep.get_dir(), "source", Root.relpath(f"{file}.dd")
        )
