import subprocess as sp
from enum import Enum, auto


class Platform(Enum):
    WINDOWS = auto()
    MACOS = auto()


current_platform = Platform.MACOS


def run_command(command: str | list[str], errlog: str | None = None) -> str:
    try:
        if isinstance(command, list):
            command = sp.list2cmdline(command)
        return sp.run(
            command, shell=True, capture_output=True, check=True
        ).stdout.decode()
    except sp.CalledProcessError as e:
        if not errlog:
            errlog = f"Failed to run {command}"
        if e.stderr:
            errlog += f": \n{e.stderr.decode()}"
        raise RuntimeError(errlog) from e
