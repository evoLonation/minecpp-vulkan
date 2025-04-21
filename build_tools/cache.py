import functools
import inspect
from os import path
import os
import pickle
from typing import Any, Callable, ParamSpec, TypeVar, get_type_hints
import subprocess as sp
from public import NinjaFile, Root, Workspace
from tool import (
    Platform,
    current_platform,
)

Param = ParamSpec("Param")
RetType = TypeVar("RetType")

enable_avoid_call: bool = True


def set_enable_avoid_call(enable: bool):
    global enable_avoid_call
    enable_avoid_call = enable


class CacheCtx:
    def __init__(self, func: Callable):
        self.name = f"{func.__module__}.{func.__qualname__}"

        class Ninja(NinjaFile):
            work_dir = self.cache_dir()

        self.Ninja = Ninja

    def cache_dir(self):
        return path.join(Workspace.cache.get_dir(), self.name)

    def param_file(self):
        return path.join(self.cache_dir(), "cache_param")

    def ret_file(self):
        return path.join(self.cache_dir(), "cache_ret")


# decorator that can avoid calls of the function if the dep_files or params is not change
# only not cache return value (just return None) when return type hint is NoneType (or empty) and cache_return is False
def cached(func: Callable[Param, RetType]) -> Callable[Param, RetType]:
    need_dep_file = False
    need_cache_param = False

    def prepare():
        nonlocal need_dep_file
        nonlocal need_cache_param
        params = inspect.signature(func).parameters
        type_hints = get_type_hints(func)
        if "cache_dep_files" in params:
            need_dep_file = True
            assert type_hints["cache_dep_files"] == list[str]
        if len(params) > (0 if not need_dep_file else 1):
            need_cache_param = True
        elif not need_dep_file:
            need_cache_param = True

    prepare()
    # print(f"need_dep_file: {need_dep_file}, need_cache_param: {need_cache_param}")

    first_init = True

    def init_ctx() -> CacheCtx:
        ctx = CacheCtx(func)
        nonlocal first_init
        if first_init:
            first_init = False
            os.makedirs(ctx.cache_dir(), exist_ok=True)
        return ctx

    @functools.wraps(func)
    def wrapper(*args, **kwargs) -> Any:
        ctx = init_ctx()
        # print(f"args: {args}, kwargs: {kwargs}")
        mixed_params = inspect.signature(func).bind(*args, **kwargs).arguments
        # print(f"mixed_params: {mixed_params}")
        param_cached = False
        if need_cache_param:
            if path.exists(ctx.param_file()):
                cached_params = pickle.load(open(ctx.param_file(), "rb"))
                if mixed_params == cached_params:
                    param_cached = True
        else:
            param_cached = True
        dep_file_cached = False
        if need_dep_file:
            if path.exists(ctx.Ninja.get_path()):
                try:
                    result = ctx.Ninja.execute()
                    dep_file_cached = result.rstrip().endswith("ninja: no work to do.")
                except sp.CalledProcessError:
                    dep_file_cached = False
        else:
            dep_file_cached = True
        cached = (param_cached and dep_file_cached) or (
            not need_dep_file and not need_cache_param
        )
        if cached and enable_avoid_call:
            print(f"cache hit: {ctx.name}")
            return pickle.load(open(ctx.ret_file(), "rb"))

        cache_dep_files = []
        if need_dep_file:
            result = func(**mixed_params, cache_dep_files=cache_dep_files)  # type: ignore
        else:
            result = func(**mixed_params)  # type: ignore
        # also need to as the target of ninja
        pickle.dump(result, open(ctx.ret_file(), "wb"))
        pickle.dump(mixed_params, open(ctx.param_file(), "wb"))
        if need_dep_file:
            with ctx.Ninja.open() as writer:
                if current_platform == Platform.WINDOWS:
                    command = "cmd.exe /c echo changed"
                elif current_platform == Platform.MACOS:
                    command = "echo changed"
                else:
                    raise RuntimeError(f"Unsupported platform: {current_platform}")
                writer.rule(
                    "changed",
                    command=command,
                    description="Checking if files have been changed",
                )
                writer.build(
                    outputs=[path.abspath(ctx.ret_file())],
                    rule="changed",
                    inputs=[path.abspath(file) for file in cache_dep_files],
                )
            ctx.Ninja.execute()
        return result

    return wrapper
