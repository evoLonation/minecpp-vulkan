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


class Workspace(Enum):
    build = "build"
    ninja = build
    gen = path.join(build, "gen")
    gen_shader = path.join(gen, "shader")
    gen_test = path.join(gen, "test")
    obj = path.join(build, "obj")
    pcm = path.join(build, "pcm")
    clangd = path.join(build, "clangd")
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
    dir = path.abspath("./")

    @staticmethod
    def set_dir(root_dir: str):
        Root.dir = path.abspath(root_dir)

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
    def execute(cls, extra: str = "", stdout=None, check=True):
        return sp.run(cls.get_command(extra), stdout=stdout, check=check)

    @classmethod
    def get_command(cls, extra: str = ""):
        file = cls.get_path()
        command = f"ninja -C {path.dirname(file)} -f {path.basename(file)} {extra}"
        return command

    @classmethod
    def dry_run(cls, target: str):
        result = cls.execute(f"{target} -n", stdout=sp.PIPE).stdout.decode("utf-8")
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


class ShaderGenNinja(NinjaFile):
    task = "shader_gen"

    class Rule:
        shader_code = "shader_code_generate"
        shader_code_total = "shader_code_total_generate"


class TestGenNinja(NinjaFile):
    task = "test_gen"


class ClangdPcmNinja(NinjaFile):
    task = "clangd_pcm"

    class Rule:
        copy = "copy"

    class Phony:
        all = "all"


class CompileCommandNinja(NinjaFile):
    task = "compile_command"

    class Rule:
        compile_command = "compile_command"


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

    base_flag = [
        clang_executable_path,
        "-std=c++23",
        "-fexperimental-library",
        "-nostdinc++",
        "-nostdlib++",
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
    def precompile(
        include_dirs: list[str], config: str | None, input: str, output: str
    ):
        return sp.list2cmdline(
            Compiler.base_flag
            + ([] if config is None else ["--config", config])
            + ["-fprebuilt-module-path=" + Workspace.pcm.get_dir()]
            + ["-isystem" + x for x in Compiler.system_include_dirs]
            + ["-I" + x for x in include_dirs]
            + ["--precompile", input, "-o", output]
        )

    @staticmethod
    def compile(include_dirs: list[str], config: str | None, input: str, output: str):
        return sp.list2cmdline(
            Compiler.base_flag
            + ([] if config is None else ["--config", config])
            + ["-fprebuilt-module-path=" + Workspace.pcm.get_dir()]
            + ["-isystem" + x for x in Compiler.system_include_dirs]
            + ["-I" + x for x in include_dirs]
            + ["-c", input, "-o", output]
        )

    @staticmethod
    def header_precompile(include_dirs: list[str], input: str, output: str):
        return sp.list2cmdline(
            Compiler.base_flag
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
            Compiler.base_flag
            + inputs
            + ["-L" + dir for dir in link_dirs + Compiler.system_link_dirs]
            + ["-l" + lib for lib in link_libs + Compiler.system_link_libs]
            + ["-o", output]
        )

    @staticmethod
    def compile_clangd(include_dirs: list[str], extra: str, input: str, output: str):
        return sp.list2cmdline(
            Compiler.base_flag
            + ["-fprebuilt-module-path=" + Compiler.pcm_clangd_dir()]
            + ["-isystem" + x for x in Compiler.system_include_dirs]
            + ["-I" + x for x in include_dirs]
            + [extra]
            + ["-c", input, "-o", output]
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
    def extract_hpcm_from_config(config_content: str) -> list[str]:
        headers = []
        for line in config_content.split("\n"):
            line = line.strip()
            if line == "":
                break
            assert line.startswith("-fmodule-file=")
            header = line[len("-fmodule-file=") :].replace("\\\\", "\\")
            headers.append(header)
        return headers

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
    def target_file(target: str):
        return path.join(Workspace.out.get_dir(), target + ".exe")

    @staticmethod
    def dynamic_dest(file: str):
        return path.join(Workspace.out.get_dir(), path.basename(file))

    @staticmethod
    def pcm_clangd_dir():
        return path.join(Workspace.clangd.get_dir(), "pcm")

    @staticmethod
    def hpcm_clangd_dir():
        return path.join(Workspace.clangd.get_dir(), "hpcm")

    @staticmethod
    def pcm_clangd_file(module: str):
        return path.join(Compiler.pcm_clangd_dir(), module.replace(":", "-") + ".pcm")

    @staticmethod
    def hpcm_clangd_file(file: str):
        return path.join(Compiler.hpcm_clangd_dir(), path.basename(file) + ".pcm")

    @staticmethod
    def to_clangd(pcm_file: str) -> str:
        if path.normpath(path.dirname(pcm_file)) == path.normpath(
            Workspace.hpcm.get_dir()
        ):
            return path.join(Compiler.hpcm_clangd_dir(), path.basename(pcm_file))
        elif path.normpath(path.dirname(pcm_file)) == path.normpath(
            Workspace.pcm.get_dir()
        ):
            return path.join(Compiler.pcm_clangd_dir(), path.basename(pcm_file))
        else:
            assert False

    # @staticmethod
    # def from_clangd(pcm_file: str) -> str:
    #     if path.normpath(path.dirname(pcm_file)) == path.normpath(
    #         Compiler.hpcm_clangd_dir()
    #     ):
    #         return path.join(Workspace.hpcm.get_dir(), path.basename(pcm_file))
    #     elif path.normpath(path.dirname(pcm_file)) == path.normpath(
    #         Compiler.pcm_clangd_dir()
    #     ):
    #         return path.join(Workspace.pcm.get_dir(), path.basename(pcm_file))
    #     else:
    #         assert False

    # @staticmethod
    # def current_clangd_uid():
    #     files = os.listdir(Workspace.clangd.get_dir())
    #     if len(files) == 0:
    #         uid = str(uuid.uuid4())
    #         os.makedirs(Compiler.pcm_clangd_dir(uid))
    #         os.makedirs(path.join(Compiler.pcm_clangd_dir(uid), "pcm"))
    #         os.makedirs(path.join(Compiler.pcm_clangd_dir(uid), "hpcm"))
    #         return uid
    #     assert len(files) == 1
    #     return files[0]

    # @staticmethod
    # def change_clangd_uid(uid: str):
    #     old_uid = Compiler.current_clangd_uid()
    #     assert old_uid != uid
    #     os.rename(
    #         path.join(Workspace.clangd.get_dir(), old_uid),
    #         path.join(Workspace.clangd.get_dir(), uid),
    #     )


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
        return path.join(Workspace.complete_dyndep.get_dir(), "module", f"{module}.dd")

    @staticmethod
    def complete_dyndep_source_file(file: str):
        return path.join(
            Workspace.complete_dyndep.get_dir(), "source", Root.relpath(f"{file}.dd")
        )
