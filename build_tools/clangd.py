from os import path
import os
import shutil
import uuid
from cache import cached
from public import (
    CompileCommandNinja,
    CompileNinja,
    Compiler,
    DepCtx,
    DepScanNinja,
    NinjaFile,
    Root,
    Workspace,
)
from resources import IncludeDir, Module, Source, Target
import subprocess as sp


def kill_process_by_name(process_name):
    try:
        # 使用 taskkill 命令通过进程名称终止所有匹配的进程
        sp.run(["taskkill", "/f", "/im", process_name], check=True)
        print(f"Process {process_name} terminated.")
    except sp.CalledProcessError as e:
        print(f"Failed to terminate process {process_name}: {e}")


# todo: execute dep scan and get header units to add in compile command
@cached("build_compile_commands")
def build_compile_commands(
    uid: str,
    modules: list[Module],
    sources: list[Source],
    targets: list[Target],
    includes: list[IncludeDir],
):
    Ninja = CompileCommandNinja
    Rule = Ninja.Rule
    with Ninja.open() as writer:
        writer.rule(
            name=Rule.compile_command,
            command=Compiler.compile(
                [x.file for x in includes],
                "$config",
                "$in",
                "$out",
                path.join(Workspace.pcm_clangd.get_dir(), uid),
            ),
        )
        for source in sources + targets + modules:
            writer.build(
                outputs=Compiler.obj_file(source.file),
                rule=Rule.compile_command,
                inputs=source.file,
                variables={"config": DepCtx.header_dep_config_file(source.file)},
            )
    result = Ninja.execute(f"-t compdb {Rule.compile_command}", stdout=sp.PIPE)
    with open(path.join(Root.dir, "compile_commands.json"), "wb") as f:
        f.write(result.stdout)


def update(
    modules: list[Module],
    sources: list[Source],
    targets: list[Target],
    includes: list[IncludeDir],
):
    print("execute dep_scan...")
    NinjaFile.execute(DepScanNinja.Phony.dep_scan, stdout=sp.DEVNULL)

    old_uid = Compiler.current_clangd_uid()

    def get_invalid_pcm() -> list[str]:
        print("execute precompile dry run...")
        result = NinjaFile.execute(
            f"{CompileNinja.Phony.pcm} -n", stdout=sp.PIPE
        ).stdout.decode("utf-8")
        lines = iter([x.strip() for x in result.split("\n")])
        assert next(lines).startswith("ninja: Entering directory")
        need_removes = []
        for line in lines:
            if line.find("PRECOMPILE ") < 0:
                break
            pcm_file = line[line.find("PRECOMPILE") + len("PRECOMPILE") :].strip()
            clangd_file = Compiler.pcm_file(
                Compiler.pcm_module(pcm_file), dir=Compiler.pcm_clangd_dir(uid=old_uid)
            )
            need_removes.append(clangd_file)
        return need_removes

    all_pcms = [
        Compiler.pcm_file(module.provide, dir=Compiler.pcm_clangd_dir(uid=old_uid))
        for module in modules
        if module.provide is not None
    ]
    invalid_pcms = get_invalid_pcm()
    valid_pcms = list(set(all_pcms) - set(invalid_pcms))

    print("copy valid pcm files...")
    for pcm in valid_pcms:
        origin = Compiler.pcm_file(Compiler.pcm_module(pcm))
        assert path.exists(origin)
        if not path.exists(pcm):
            shutil.copy(origin, pcm)
            print(f"copy {origin} to {pcm}")

    if len(invalid_pcms) == 0:
        print("no need to update invalid pcms, done.")
        return
    need_remove_pcms = [x for x in invalid_pcms if path.exists(x)]
    need_rename = len(need_remove_pcms) != 0
    uid = ""
    if need_rename:
        print("change clangd pcm directory...")
        uid = str(uuid.uuid4())
        build_compile_commands(uid, modules, sources, targets, includes)
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
        print("no need to delete invalid pcms, not rename")

    print("execute precompile...")
    command = NinjaFile.get_command(f"{CompileNinja.Phony.pcm} -k 0")
    process = sp.Popen(
        command,
        stdout=sp.PIPE,
        stderr=sp.PIPE,
        text=True,
        bufsize=1,
        universal_newlines=True,
    )

    def get_line():
        assert process.stdout is not None
        line = process.stdout.readline()
        if line == "" and process.poll() is not None:
            return None
        # print(line)
        return line

    assert str(get_line()).strip().startswith("ninja: Entering directory")
    # maybe:
    # PRECOMPILE xxx
    # normal output
    # PRECOMPILE xxx
    # FAILED: xxx
    last_pcm_file: str = ""
    error_msg: str | None = None
    normal_msg: str = ""
    error_dict: dict[str, str] = {}

    def try_copy():
        nonlocal error_msg
        if error_msg is not None:
            error_dict[last_pcm_file] = error_msg
            error_msg = None
        elif last_pcm_file != "":
            nonlocal normal_msg
            if normal_msg != "":
                print(normal_msg)
                normal_msg = ""
            assert path.exists(last_pcm_file)
            clangd_file = Compiler.pcm_file(
                Compiler.pcm_module(last_pcm_file), Compiler.pcm_clangd_dir(uid=old_uid)
            )
            assert not path.exists(clangd_file)
            shutil.copy(last_pcm_file, clangd_file)
            print(f"copy {last_pcm_file} to {clangd_file}")

    while True:
        line = get_line()
        if line is None:
            try_copy()
            break
        elif line.find("] PRECOMPILE ") >= 0:
            try_copy()
            last_pcm_file = line[line.find("PRECOMPILE") + len("PRECOMPILE") :].strip()
        elif line.startswith("FAILED: "):
            assert path.normpath(last_pcm_file) == path.normpath(
                line[len("FAILED: ") :].strip()
            )
            error_msg = ""
        elif (
            line.strip()
            == "ninja: build stopped: cannot make progress due to previous errors."
        ):
            try_copy()
            break
        else:
            if error_msg is not None:
                error_msg += line
            else:
                normal_msg += line
    if process.stderr is not None:
        stderr = process.stderr.read()
        if len(stderr) != 0:
            print(f"ninja stderr:\n {stderr}")
    if len(error_dict) != 0:
        print("occurred errors:")
        for k, v in error_dict.items():
            print(f"{k}: \n{v}")
    if need_rename:
        assert uid != ""
        assert not path.exists(Compiler.pcm_clangd_dir(uid=uid))
        shutil.move(
            Compiler.pcm_clangd_dir(uid=old_uid), Compiler.pcm_clangd_dir(uid=uid)
        )
