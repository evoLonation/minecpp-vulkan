from dataclasses import dataclass
from os import path
import os
import re
import threading
from typing import cast
from cache import cached
from public import (
    HeaderNinja,
    CompileNinja,
    DepScanNinja,
    NinjaFile,
    Root,
)
import subprocess as sp


def kill_process_by_name(process_name: str):
    try:
        sp.run(["taskkill", "/f", "/im", process_name], check=True)
        print(f"Process {process_name} terminated.")
    except sp.CalledProcessError as e:
        print(f"Failed to terminate process {process_name}: {e}")


def get_compile_commands_content() -> bytes:
    Rule = CompileNinja.Rule
    result = NinjaFile.execute(
        f"-t compdb {Rule.compile} {Rule.precompile}", stdout=sp.PIPE
    )
    return result.stdout


@cached
def write_compile_commands_json(content: str | bytes):
    if isinstance(content, str):
        content = content.encode("utf-8")
    with open(path.join(Root.dir, "compile_commands.json"), "wb") as f:
        f.write(content)


def dry_run_precompile() -> list[str]:
    outputs = []
    for output in NinjaFile.dry_run(
        f"{HeaderNinja.Phony.header_unit} {CompileNinja.Phony.pcm}"
    ):
        output = output.strip()
        prefixes = ["PRECOMPILE", "HEADERUNIT PRECOMPILE"]
        for prefix in prefixes:
            if output.startswith(prefix):
                output = output[len(prefix) :].strip()
                outputs.append(output)
    return outputs


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


def execute_precompile() -> PrecompileResult:
    command = NinjaFile.get_command(
        f"{HeaderNinja.Phony.header_unit} {CompileNinja.Phony.pcm} -k 0"
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

    pattern = re.compile(r"\[\d+/\d+\] (?:PRECOMPILE|HEADERUNIT PRECOMPILE) (\S+)")
    for line in lines[1:]:
        if match := pattern.match(line):
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


def update():
    print("execute dep_scan...")
    # if dry run need dyndep but not exist, it will occur error
    NinjaFile.execute(DepScanNinja.Phony.dep_scan)

    print("dry run precompile pcms...")
    invalid_pcms = dry_run_precompile()
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

    print("execute precompile pcms...")
    res = execute_precompile()
    print("normal messages:")
    for k, v in res.normal_msgs.items():
        print(f"{k}:\n{v}")
    print("error messages:")
    for k, v in res.error_msgs.items():
        print(f"{k}:\n{v}")

    write_compile_commands_json(get_compile_commands_content())
