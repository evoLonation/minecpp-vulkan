import inspect
from os import path
import os
import pickle
from typing import Any, Callable, ParamSpec, TypeVar, get_type_hints
import subprocess as sp
from public import NinjaCtx, PathCtx, Workspace


Param = ParamSpec("Param")
RetType = TypeVar("RetType")


class CacheCtx:
    def __init__(self, name: str):
        self.name = name

    def cache_dir(self):
        return path.join(PathCtx.get_dir(Workspace.cache), self.name)

    def ninja_file(self):
        return path.join(self.cache_dir(), "build.ninja")

    def param_file(self):
        return path.join(self.cache_dir(), "cache_param")

    def ret_file(self):
        return path.join(self.cache_dir(), "cache_ret")


# return a decorator that can avoid calls of the function if the dep_files or params is not change
# only not cache return value (just return None) when return type hint is NoneType (or empty) and cache_return is False
def cached(name: str):
    ctx = CacheCtx(name)
    os.makedirs(ctx.cache_dir(), exist_ok=True)

    def decorator(func: Callable[Param, RetType]) -> Callable[Param, RetType]:
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

        def wrapper(*args, **kwargs) -> Any:
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
                if path.exists(ctx.ninja_file()):
                    result = NinjaCtx.execute(ctx.ninja_file(), stdout=sp.PIPE)
                    dep_file_cached = (
                        result.stdout.decode("utf-8")
                        .rstrip()
                        .endswith("ninja: no work to do.")
                    )
            else:
                dep_file_cached = True
            cached = (param_cached and dep_file_cached) or (
                not need_dep_file and not need_cache_param
            )
            if cached:
                print(f"cache hit: {name}")
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
                with NinjaCtx.open_ninja(ctx.ninja_file()) as writer:
                    writer.rule(
                        "changed",
                        command='cmd.exe /c echo "changed"',
                        description="Checking if files have been changed",
                    )
                    writer.build(
                        outputs=[path.abspath(ctx.ret_file())],
                        rule="changed",
                        inputs=[path.abspath(file) for file in cache_dep_files],
                    )
                NinjaCtx.execute(ctx.ninja_file(), stdout=sp.PIPE)
            return result

        return wrapper

    return decorator
