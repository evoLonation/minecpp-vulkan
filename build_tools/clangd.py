from dataclasses import dataclass
from os import path
import os
import re
import threading
from typing import Literal, cast
from cache import cached
from public import (
    ClangdPcmNinja,
    CompileCommandNinja,
    HeaderNinja,
    CompileNinja,
    Compiler,
    DepCtx,
    DepScanNinja,
    NinjaFile,
    Root,
)
from resources import HeaderUnit, IncludeDir, Module, Source, Target
import subprocess as sp


def kill_process_by_name(process_name: str):
    try:
        sp.run(["taskkill", "/f", "/im", process_name], check=True)
        print(f"Process {process_name} terminated.")
    except sp.CalledProcessError as e:
        print(f"Failed to terminate process {process_name}: {e}")


@cached
def build_pcm_ninja(
    headers: list[HeaderUnit],
    modules: list[Module],
):
    Ninja = ClangdPcmNinja
    Rule = Ninja.Rule
    Phony = Ninja.Phony
    with Ninja.open() as writer:
        writer.rule(
            name=Rule.copy,
            command="cmd.exe /c copy /Y $in $out  > NUL",
            description="COPY CLANGD $out",
        )
        module_names = [x.provide for x in modules if x.provide is not None]
        header_files = [x.file for x in headers]
        for file in header_files:
            writer.build(
                outputs=Compiler.hpcm_clangd_file(file),
                rule=Rule.copy,
                inputs=Compiler.hpcm_file(file),
            )
        for module in module_names:
            writer.build(
                outputs=Compiler.pcm_clangd_file(module),
                rule=Rule.copy,
                inputs=Compiler.pcm_file(module),
            )
        writer.build(
            outputs=Phony.all,
            rule="phony",
            inputs=[Compiler.pcm_clangd_file(x) for x in module_names]
            + [Compiler.hpcm_clangd_file(x) for x in header_files],
        )


@cached
def get_compile_commands_content(
    modules: list[Module],
    sources: list[Source],
    targets: list[Target],
    includes: list[IncludeDir],
    cache_dep_files: list[str] = [],
) -> bytes:
    Ninja = CompileCommandNinja
    Rule = Ninja.Rule
    with Ninja.open() as writer:
        writer.rule(
            name=Rule.compile_command,
            command=Compiler.compile_clangd(
                [x.file for x in includes], "$extra", "$in", "$out"
            ),
        )
        for source in sources + targets + modules:
            config_file = DepCtx.header_dep_config_file(source.file)
            cache_dep_files.append(config_file)
            with open(config_file, "rt") as f:
                extra = Compiler.hpcm_flag(
                    [
                        Compiler.to_clangd(x)
                        for x in Compiler.extract_hpcm_from_config(f.read())
                    ],
                    False,
                )
            writer.build(
                outputs=Compiler.obj_file(source.file),
                rule=Rule.compile_command,
                inputs=source.file,
                variables={"extra": extra},
            )
    result = Ninja.execute(f"-t compdb {Rule.compile_command}", stdout=sp.PIPE)
    return result.stdout


@cached
def write_compile_commands_json(content: str | bytes):
    if isinstance(content, str):
        content = content.encode("utf-8")
    with open(path.join(Root.dir, "compile_commands.json"), "wb") as f:
        f.write(content)


def get_dry_run_outputs() -> list[str]:
    outputs = []
    prefix = "COPY CLANGD"
    for output in ClangdPcmNinja.dry_run(ClangdPcmNinja.Phony.all):
        output = output.strip()
        if output.startswith(prefix):
            output = output[len(prefix) :].strip()
            outputs.append(output)
    return outputs


def get_invalid_clangd_pcm(invalid_pcms: list[str]):
    return [y for y in [Compiler.to_clangd(x) for x in invalid_pcms] if path.exists(y)]


def remove_invalid_pcm(invalid_clangd_pcms: list[str]):
    for pcm in invalid_clangd_pcms:
        try:
            os.remove(pcm)
            print(f"remove {pcm}")
        except Exception as e:
            e.add_note(f"failed to remove {pcm}")
            raise


def execute_command(command: str) -> list[str]:
    process = sp.Popen(command, stdout=sp.PIPE, stderr=sp.PIPE, text=True, bufsize=1)
    outputs = []

    def read_output():
        assert process.stdout is not None
        for line in process.stdout:
            print(line, end="")
            cast(list, outputs).append(line)

    output_thread = threading.Thread(target=read_output)
    output_thread.start()
    process.wait()
    output_thread.join()
    return outputs


@dataclass
class PrecompileResult:
    outputs: list[str]
    normal_msgs: dict[str, str]
    error_msgs: dict[str, str]


def execute_precompile(type: Literal["pcm", "hpcm"]) -> PrecompileResult:
    command = NinjaFile.get_command(
        f"{CompileNinja.Phony.pcm if type == 'pcm' else HeaderNinja.Phony.header_unit} -k 0"
    )
    lines = execute_command(command)
    assert lines[0].strip().startswith("ninja: Entering directory")
    if lines[1].startswith("ninja: no work to do."):
        return PrecompileResult([], {}, {})
    # maybe:
    # PRECOMPILE xxx
    # normal output
    # PRECOMPILE xxx
    # FAILED: xxx
    outputs: list[str] = []
    last_output: str = ""
    error_msg: str | None = None
    normal_msg: str = ""
    error_dict: dict[str, str] = {}
    normal_dict: dict[str, str] = {}

    def save_current():
        nonlocal error_msg
        nonlocal normal_msg
        if error_msg is not None:
            error_dict[last_output] = error_msg
            error_msg = None
        elif last_output != "":
            if normal_msg != "":
                normal_dict[last_output] = normal_msg
                normal_msg = ""
            assert path.exists(last_output)
            outputs.append(last_output)

    pattern = (
        re.compile(r"\[\d+/\d+\] PRECOMPILE (\S+)")
        if type == "pcm"
        else re.compile(r"\[\d+/\d+\] HEADERUNIT PRECOMPILE (\S+)")
    )
    for line in lines[1:]:
        match = pattern.match(line)
        if match is not None:
            save_current()
            last_output = match.group(1)
        elif line.startswith("FAILED: "):
            assert path.normpath(last_output) == path.normpath(
                line[len("FAILED: ") :].strip()
            )
            error_msg = ""
        elif line.startswith(
            "ninja: build stopped: cannot make progress due to previous errors."
        ):
            assert error_msg is not None
            break
        else:
            if error_msg is not None:
                error_msg += line
            else:
                normal_msg += line
    save_current()
    return PrecompileResult(outputs, normal_dict, error_dict)


def update(
    headers: list[HeaderUnit],
    modules: list[Module],
    sources: list[Source],
    targets: list[Target],
    includes: list[IncludeDir],
):
    print("execute dep_scan...")
    NinjaFile.execute(DepScanNinja.Phony.dep_scan)

    print("execute header precompile...")
    res = execute_precompile("hpcm")
    print("normal messages:")
    for k, v in res.normal_msgs.items():
        print(f"{k}:\n{v}")
    print("error messages:")
    for k, v in res.error_msgs.items():
        print(f"{k}:\n{v}")
    print("execute pcm precompile...")
    res = execute_precompile("pcm")
    print("normal messages:")
    for k, v in res.normal_msgs.items():
        print(f"{k}:\n{v}")
    print("error messages:")
    for k, v in res.error_msgs.items():
        print(f"{k}:\n{v}")

    build_pcm_ninja(headers, modules)
    print("dry run clangd copy pcm files...")
    invalid_pcms = get_dry_run_outputs()
    need_remove_pcms = [x for x in invalid_pcms if path.exists(x)]
    need_remove = len(need_remove_pcms) > 0
    if need_remove:
        print(
            "need remove invalid pcm files in changd dir, so rebuild compile_commands with empty..."
        )
        write_compile_commands_json("[]")
        kill_process_by_name("clangd.exe")
        print("remove invalid pcm files...")
        for pcm in need_remove_pcms:
            try:
                os.remove(pcm)
                print(f"remove {pcm}")
            except Exception as e:
                e.add_note(f"failed to remove {pcm}")
                raise
    else:
        print("no need to remove invalid pcm files...")

    print("execute clangd pcm copy...")
    ClangdPcmNinja.execute(ClangdPcmNinja.Phony.all)
    write_compile_commands_json(
        get_compile_commands_content(modules, sources, targets, includes)
    )
