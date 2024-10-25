from dataclasses import dataclass
import json
from os import path
import os
import re
import shutil
import threading
from typing import Literal, cast
import uuid
from cache import cached
from public import (
    CompileCommandNinja,
    HeaderNinja,
    CompileNinja,
    Compiler,
    DepCtx,
    DepScanNinja,
    NinjaFile,
    Root,
    Workspace,
)
from resources import HeaderUnit, IncludeDir, Module, Source, Target
import subprocess as sp


def kill_process_by_name(process_name: str):
    try:
        sp.run(["taskkill", "/f", "/im", process_name], check=True)
        print(f"Process {process_name} terminated.")
    except sp.CalledProcessError as e:
        print(f"Failed to terminate process {process_name}: {e}")


# need dep scan first
@cached("build_compile_commands")
def build_compile_commands(
    modules: list[Module],
    sources: list[Source],
    targets: list[Target],
    includes: list[IncludeDir],
    uid: str,
    cache_dep_files: list[str] = [],
):
    Ninja = CompileCommandNinja
    Rule = Ninja.Rule
    with Ninja.open() as writer:
        writer.rule(
            name=Rule.compile_command,
            command=Compiler.compile_clangd(
                [x.file for x in includes], "$extra", "$in", "$out", uid
            ),
        )
        for source in sources + targets + modules:
            config_file = DepCtx.header_dep_config_file(source.file)
            cache_dep_files.append(config_file)
            with open(config_file, "rt") as f:
                extra = Compiler.hpcm_flag(
                    Compiler.extract_hpcm_from_config(f.read()), False
                )
            writer.build(
                outputs=Compiler.obj_file(source.file),
                rule=Rule.compile_command,
                inputs=source.file,
                variables={"extra": extra},
            )
    result = Ninja.execute(f"-t compdb {Rule.compile_command}", stdout=sp.PIPE)
    with open(path.join(Root.dir, "compile_commands.json"), "wb") as f:
        f.write(result.stdout)


def dry_run(target: str) -> list[str]:
    result = NinjaFile.execute(f"{target} -n", stdout=sp.PIPE).stdout.decode("utf-8")
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


def get_dry_run_outputs(target: str, prefix: str) -> list[str]:
    outputs = []
    for output in dry_run(target):
        output = output.strip()
        assert output.startswith(prefix), f"output: {output}, prefix: {prefix}"
        output = output[len(prefix) :].strip()
        outputs.append(output)
    return outputs


def get_valid_and_invalid_pcms(
    type: Literal["hpcm", "pcm"], resources: list[Module] | list[HeaderUnit]
) -> tuple[list[str], list[str]]:
    if type == "hpcm":
        all_pcms = [
            Compiler.hpcm_file(x.file) for x in cast(list[HeaderUnit], resources)
        ]
        invalid_pcms = get_dry_run_outputs(
            HeaderNinja.Phony.header_unit, "HEADERUNIT PRECOMPILE"
        )
    elif type == "pcm":
        all_pcms = [
            Compiler.pcm_file(x.provide)
            for x in cast(list[Module], resources)
            if x.provide is not None
        ]
        invalid_pcms = get_dry_run_outputs(CompileNinja.Phony.pcm, "PRECOMPILE")
    # invalid_pcms = [Compiler.to_clangd(x) for x in invalid_pcms]
    valid_pcms = list(set(all_pcms) - set(invalid_pcms))
    return valid_pcms, invalid_pcms


def copy_pcm_to_clangd(pcm_files: list[str]):
    for src in pcm_files:
        dst = Compiler.to_clangd(src)
        assert path.exists(src)
        if not path.exists(dst):
            shutil.copy(src, dst)
            print(f"  copy {src} to {dst}")


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


def execute_precompile(type: Literal["hpcm", "pcm"]) -> PrecompileResult:
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


def update_clangd_pcms(
    headers: list[HeaderUnit],
    modules: list[Module],
    sources: list[Source],
    targets: list[Target],
    includes: list[IncludeDir],
):
    print("execute dep_scan...")
    NinjaFile.execute(DepScanNinja.Phony.dep_scan)

    # now is in build dir
    valid_hpcms, invalid_hpcms = get_valid_and_invalid_pcms("hpcm", headers)
    print("execute header precompile...")
    hpcm_res = execute_precompile("hpcm")
    print("normal messages:")
    for k, v in hpcm_res.normal_msgs.items():
        print(f"{k}:\n{v}")
    print("error messages:")
    for k, v in hpcm_res.error_msgs.items():
        print(f"{k}:\n{v}")
    valid_pcms, invalid_pcms = get_valid_and_invalid_pcms("pcm", modules)
    print("execute pcm precompile...")
    pcm_res = execute_precompile("pcm")
    print("normal messages:")
    for k, v in pcm_res.normal_msgs.items():
        print(f"{k}:\n{v}")
    print("error messages:")
    for k, v in pcm_res.error_msgs.items():
        print(f"{k}:\n{v}")

    print(f"copy valid pcm files...")
    copy_pcm_to_clangd(valid_hpcms)
    print(f"copy valid hpcm files...")
    copy_pcm_to_clangd(valid_pcms)

    if len(invalid_hpcms) == 0 and len(invalid_pcms) == 0:
        print("no need to update invalid pcms, done.")
        # update header deps
        build_compile_commands(
            modules, sources, targets, includes, uid=Compiler.current_clangd_uid()
        )
        return

    # now is in clangd dir
    invalid_hpcms = get_invalid_clangd_pcm(invalid_hpcms)
    invalid_pcms = get_invalid_clangd_pcm(invalid_pcms)

    need_rename = len(invalid_hpcms) != 0 or len(invalid_pcms) != 0
    uid = ""

    if need_rename:
        print(
            "need remove invalid pcm files in changd dir, so rebuild compile_commands with new uid..."
        )
        uid = str(uuid.uuid4())
        build_compile_commands(modules, sources, targets, includes, uid)
        kill_process_by_name("clangd.exe")
        print("remove invalid hpcm files...")
        remove_invalid_pcm(invalid_hpcms)
        print("remove invalid pcm files...")
        remove_invalid_pcm(invalid_pcms)
    else:
        print("no need to remove invalid pcm files...")
        # update header deps
        build_compile_commands(
            modules, sources, targets, includes, uid=Compiler.current_clangd_uid()
        )

    # print("execute header precompile...")
    # res = execute_precompile("hpcm")
    # print("normal messages:")
    # for k, v in res.normal_msgs.items():
    #     print(f"{k}:\n{v}")
    # print("error messages:")
    # for k, v in res.error_msgs.items():
    #     print(f"{k}:\n{v}")
    copy_pcm_to_clangd(hpcm_res.outputs)

    # print("execute pcm precompile...")
    # res = execute_precompile("pcm")
    # print("normal messages:")
    # for k, v in res.normal_msgs.items():
    #     print(f"{k}:\n{v}")
    # print("error messages:")
    # for k, v in res.error_msgs.items():
    #     print(f"{k}:\n{v}")
    copy_pcm_to_clangd(pcm_res.outputs)

    if need_rename:
        assert uid != ""
        Compiler.change_clangd_uid(uid)
