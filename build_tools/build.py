import argparse
from dataclasses import dataclass, field, fields
import inspect
import os
from types import NoneType
from dacite import from_dict
import dacite
from enum import Enum
import os.path as path
from typing import Annotated, Any, Callable, Literal, ParamSpec, TypeVar, get_type_hints
import yaml
from cache import cached
import shader_gen
import ninja_syntax as ninja
import pickle
import subprocess as sp
from public import Compiler, DepCtx, PathCtx, NinjaCtx, Script, Workspace
from resources import (
    get_file_resources,
    Module,
    Source,
    IncludeDir,
    HeaderUnit,
    LibFile,
    DylibFile,
    Shader,
    Test,
    Target,
    save_resources,
)


@cached("build_gen_shader")
def build_gen_shader(resources: list[Shader]) -> list[Module]:
    with NinjaCtx.open_ninja(NinjaCtx.Task.shader_gen) as ninja_writer:
        command = Script.get_command(
            Script.shader_gen,
            ["single", "$in", "$module", "$out"],
        )
        ninja_writer.rule(
            name=NinjaCtx.Rule.shader_code,
            command=command,
            description="SHADERCODE single generate $out",
        )
        gen_files = []
        module_names = []
        for shader in resources:
            gen_files.append(
                path.join(
                    PathCtx.get_dir(Workspace.gen_shader),
                    PathCtx.rel_root_path(shader.file) + ".ccm",
                )
            )
            module_names.append(f"render.vk.shader_code.{path.basename(shader.file)}")
        for shader, output, module in zip(resources, gen_files, module_names):
            ninja_writer.build(
                rule=NinjaCtx.Rule.shader_code,
                outputs=output,
                inputs=shader.file,
                variables={"module": module},
            )
        module_resources: list[Module] = []
        for file, module in zip(gen_files, module_names):
            module = Module(file=file, provide=module)
            module_resources.append(module)

        command = Script.get_command(
            Script.shader_gen,
            ["total", "$module", "$modules", "$out"],
        )
        # 生成 total shader_code
        ninja_writer.rule(
            name=NinjaCtx.Rule.shader_code_total,
            command=command,
            description="SHADERCODE total generate $out",
        )
        total_output = path.join(
            PathCtx.get_dir(Workspace.gen_shader), "shader_code.cc"
        )
        total_module_name = "render.vk.shader_code"
        ninja_writer.build(
            rule=NinjaCtx.Rule.shader_code_total,
            outputs=total_output,
            variables={
                "module": total_module_name,
                "modules": " ".join([x for x in module_names]),
            },
        )
        module_resources.append(Module(file=total_output, implement=total_module_name))
    return module_resources


@cached("build_gen_test")
def build_gen_test(resources: list[Test]) -> list[Target]:
    with NinjaCtx.open_ninja(NinjaCtx.Task.test_gen) as writer:
        pass
    return []


@cached("build_precompile_headers")
def build_precompile_headers(
    header_units: list[HeaderUnit], include_dirs: list[IncludeDir]
):
    with NinjaCtx.open_ninja(NinjaCtx.Task.header_precompile) as writer:
        writer.rule(
            NinjaCtx.Rule.precompile_header,
            Compiler.header_precompile([x.file for x in include_dirs], "$in", "$out"),
            description=f"HEADERUNIT PRECOMPILE $out",
        )
        header_pcm_outputs = []
        for header_unit in header_units:
            output = Compiler.header_pcm_file(header_unit.file)
            writer.build(
                outputs=output,
                rule=NinjaCtx.Rule.precompile_header,
                inputs=header_unit.file,
            )
            header_pcm_outputs.append(output)
        writer.build(
            outputs=NinjaCtx.Phony.header_unit,
            rule="phony",
            inputs=header_pcm_outputs,
        )


@cached("build_dep_scan")
def build_dep_scan(
    modules: list[Module],
    sources: list[Source],
    targets: list[Target],
    includes: list[IncludeDir],
):
    with NinjaCtx.open_ninja(NinjaCtx.Task.dep_scan) as writer:
        command = Script.get_command(
            Script.dep_scan,
            ["-c", "$in"]
            + ["$module_arg"]
            + ["--includes", *[x.file for x in includes]]
            + ["--root_dir", PathCtx.root_dir],
        )
        writer.rule(
            name=NinjaCtx.Rule.dep_scan,
            command=command,
            description=f"Dependency scan $out",
        )

        def build_ninja(file: str, module_arg: str):
            writer.build(
                outputs=[
                    DepCtx.dyndep_file(file),
                    DepCtx.header_dep_config_file(file),
                    DepCtx.module_dep_file(file),
                ],
                rule=NinjaCtx.Rule.dep_scan,
                inputs=file,
                variables={"module_arg": module_arg},
            )

        for source in sources:
            build_ninja(source.file, "")
        for target in targets:
            build_ninja(target.file, "")
        for module in modules:
            if module.implement != None:
                build_ninja(module.file, f"--implement {module.implement}")
            else:
                build_ninja(module.file, f"--provide {module.provide}")


def build_compile(
    modules: list[Module],
    sources: list[Source],
    targets: list[Target],
    includes: list[IncludeDir],
):
    with NinjaCtx.open_ninja(NinjaCtx.Task.compile) as writer:
        writer.rule(
            name=NinjaCtx.Rule.precompile,
            command=Compiler.precompile(
                [x.file for x in includes], "$config", "$in", "$out"
            ),
        )
        writer.rule(
            name=NinjaCtx.Rule.compile,
            command=Compiler.compile(
                [x.file for x in includes], "$config", "$in", "$out"
            ),
        )

        def build(rule: str, source: str, input: str, output: str):
            writer.build(
                outputs=output,
                rule=rule,
                inputs=input,
                order_only=DepCtx.dyndep_file(source),
                variables={
                    "dyndep": DepCtx.dyndep_file(source),
                    "config": DepCtx.header_dep_config_file(source),
                },
            )

        for source in sources:
            build(
                NinjaCtx.Rule.compile,
                source.file,
                source.file,
                Compiler.obj_file(source.file),
            )
        for target in targets:
            build(
                NinjaCtx.Rule.compile,
                target.file,
                target.file,
                Compiler.obj_file(target.file),
            )
        for module in modules:
            if module.provide != None:
                build(
                    NinjaCtx.Rule.precompile,
                    module.file,
                    module.file,
                    Compiler.pcm_file(module.provide),
                )
                build(
                    NinjaCtx.Rule.compile,
                    module.file,
                    Compiler.pcm_file(module.provide),
                    Compiler.obj_file(module.file),
                )
            else:
                build(
                    NinjaCtx.Rule.compile,
                    module.file,
                    module.file,
                    Compiler.obj_file(module.file),
                )


# a module phony A will build all relative files needed by module A (whole dependency tree)
@cached("build_complete_dep")
def build_complete_dep(
    modules: list[Module], sources: list[Source], targets: list[Target]
):
    module_map: dict[str, list[str]] = {}
    for module in modules:
        module_name = module.implement if module.provide is None else module.provide
        module_map.setdefault(str(module_name), []).append(module.file)
    with NinjaCtx.open_ninja(NinjaCtx.Task.complete_dep) as writer:
        writer.rule(
            name=NinjaCtx.Rule.complete_dyndep,
            command=Script.get_command(
                Script.complete_dyndep,
                [PathCtx.root_dir, "$phony", "$module", "$in", "$out"],
            ),
        )
        for module, files in module_map.items():
            writer.build(
                outputs=NinjaCtx.Phony.complete_dep_module(module),
                rule="phony",
                inputs=[Compiler.obj_file(file) for file in files],
                order_only=DepCtx.complete_dyndep_module_file(module),
                variables={"dyndep": DepCtx.complete_dyndep_module_file(module)},
            )
            writer.build(
                outputs=DepCtx.complete_dyndep_module_file(module),
                rule=NinjaCtx.Rule.complete_dyndep,
                inputs=[DepCtx.module_dep_file(file) for file in files],
                variables={
                    "module": f"--module {module}",
                    "phony": NinjaCtx.Phony.complete_dep_module(module),
                },
            )
        for source in [*sources, *targets]:
            file = source.file
            writer.build(
                outputs=NinjaCtx.Phony.complete_dep_source(file),
                rule="phony",
                inputs=Compiler.obj_file(file),
                order_only=DepCtx.complete_dyndep_source_file(file),
                variables={"dyndep": DepCtx.complete_dyndep_source_file(file)},
            )
            writer.build(
                outputs=DepCtx.complete_dyndep_source_file(file),
                rule=NinjaCtx.Rule.complete_dyndep,
                inputs=DepCtx.module_dep_file(file),
                variables={
                    "module": "",
                    "phony": NinjaCtx.Phony.complete_dep_source(file),
                },
            )


@cached("build_target")
def build_target(
    targets: list[Target], sources: list[Source], dynamic_libs: list[DylibFile]
):
    with NinjaCtx.open_ninja(NinjaCtx.Task.target) as writer:
        writer.rule(
            name=NinjaCtx.Rule.link,
            command=Script.get_command(
                Script.link,
                [PathCtx.root_dir, "$input", "$out"],
            ),
        )
        writer.rule(
            name=NinjaCtx.Rule.copy,
            command="cmd.exe /c copy /Y $in $out  > NUL",
            description="COPY dynamic library $out",
        )
        for target in targets:
            writer.build(
                outputs=Compiler.target_file(target.name),
                rule=NinjaCtx.Rule.link,
                implicit=[
                    NinjaCtx.Phony.complete_dep_source(source.file)
                    for source in sources + [target]
                ],
                variables={"input": target.file},
            )
            for dylib in dynamic_libs:
                writer.build(
                    outputs=Compiler.dynamic_dir(dylib.file),
                    rule=NinjaCtx.Rule.copy,
                    inputs=dylib.file,
                )
            writer.build(
                outputs=NinjaCtx.Phony.target(target.name),
                rule="phony",
                inputs=[Compiler.target_file(target.name)]
                + [Compiler.dynamic_dir(dylib.file) for dylib in dynamic_libs],
            )


@cached("build_total")
def build_total():
    with NinjaCtx.open_ninja(NinjaCtx.Task.total) as writer:
        writer.subninja(NinjaCtx.Task.dep_scan.value)
        writer.subninja(NinjaCtx.Task.header_precompile.value)
        writer.subninja(NinjaCtx.Task.compile.value)
        writer.subninja(NinjaCtx.Task.complete_dep.value)
        writer.subninja(NinjaCtx.Task.target.value)
        writer.subninja(NinjaCtx.Task.shader_gen.value)
        writer.subninja(NinjaCtx.Task.test_gen.value)


@cached("generate_compile_commands")
def generate_compile_commands(cache_dep_files: list[str] = []):
    cache_dep_files.append(NinjaCtx.get_file(NinjaCtx.Task.compile))
    result = NinjaCtx.execute(
        NinjaCtx.Task.compile,
        f"-t compdb {NinjaCtx.Rule.precompile} {NinjaCtx.Rule.compile}",
        stdout=sp.PIPE,
    )
    with open(path.join(PathCtx.root_dir, "compile_commands.json"), "wb") as f:
        f.write(result.stdout)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root_dir", type=str, dest="root_dir", default="./")
    args = parser.parse_args()
    PathCtx.set_root_dir(args.root_dir)
    PathCtx.mkdirs()

    resources = get_file_resources()
    # print(f"resources: {resources}")

    resources.modules.extend(build_gen_shader(resources.shaders))
    resources.targets.extend(build_gen_test(resources.tests))
    save_resources(resources)

    build_precompile_headers(resources.header_units, resources.include_dirs)
    build_dep_scan(
        resources.modules, resources.sources, resources.targets, resources.include_dirs
    )
    build_compile(
        resources.modules, resources.sources, resources.targets, resources.include_dirs
    )
    generate_compile_commands()
    build_complete_dep(resources.modules, resources.sources, resources.targets)
    build_target(resources.targets, resources.sources, resources.dylib_files)

    build_total()


if __name__ == "__main__":
    main()
