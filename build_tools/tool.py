import subprocess as sp
from enum import Enum, auto
from typing import Any, Literal, overload
import platform


class Platform(Enum):
    WINDOWS = auto()
    MACOS = auto()


system = platform.system()
if system == "Windows":
    current_platform = Platform.WINDOWS
elif system == "Darwin":
    current_platform = Platform.MACOS
else:
    raise RuntimeError(f"Unsupported platform: {system}")


class OutputMode(Enum):
    STRING = auto()
    RAW = auto()
    NO_CAPTURE = auto()


@overload
def run_command(
    command: str | list[str],
    output: Literal[OutputMode.STRING] = OutputMode.STRING,
    errlog: str | None = None,
    cwd: Any = None,
) -> str: ...


@overload
def run_command(
    command: str | list[str],
    output: Literal[OutputMode.RAW],
    errlog: str | None = None,
    cwd: Any = None,
) -> bytes: ...


@overload
def run_command(
    command: str | list[str],
    output: Literal[OutputMode.NO_CAPTURE],
    errlog: str | None = None,
    cwd: Any = None,
) -> None: ...


def run_command(
    command: str | list[str],
    output: OutputMode = OutputMode.STRING,
    errlog: str | None = None,
    cwd: Any = None,
) -> str | bytes | None:
    try:
        if isinstance(command, list):
            command = sp.list2cmdline(command)
        ret = sp.run(
            command,
            shell=True,
            capture_output=output != OutputMode.NO_CAPTURE,
            check=True,
            cwd=cwd,
        )
        if output == OutputMode.STRING:
            return ret.stdout.decode()
        elif output == OutputMode.RAW:
            return ret.stdout
        else:
            return
    except sp.CalledProcessError as e:
        if not errlog:
            errlog = f"Failed to run {command}"
        if e.stderr:
            errlog += f": \n{e.stderr.decode()}"
        raise RuntimeError(errlog) from e
