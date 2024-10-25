import argparse
import os.path as path
from clangd import build_compile_commands, update_clangd_pcms
from cache import cached
import subprocess as sp
from public import (
    CompileCommandNinja,
    Compiler,
    DepCtx,
    HeaderNinja,
    NinjaFile,
    Root,
    Script,
    Workspace,
    ShaderGenNinja,
    CompileNinja,
    DepScanNinja,
    CompleteDepNinja,
    TargetNinja,
    TestGenNinja,
)
from resources import (
    get_file_resources,
    Module,
    Source,
    IncludeDir,
    HeaderUnit,
    DylibFile,
    Shader,
    Test,
    Target,
    save_resources,
)


@cached("build_gen_shader")
def build_gen_shader(resources: list[Shader]) -> list[Module]:
    Rule = ShaderGenNinja.Rule
    with ShaderGenNinja.open() as ninja_writer:
        command = Script.get_command(
            Script.shader_gen,
            ["single", "$in", "$module", "$out"],
        )
        ninja_writer.rule(
            name=Rule.shader_code,
            command=command,
            description="SHADERCODE single generate $out",
        )
        gen_files = []
        module_names = []
        for shader in resources:
            gen_files.append(
                path.join(
                    Workspace.gen_shader.get_dir(), Root.relpath(shader.file) + ".ccm"
                )
            )
            module_names.append(f"render.vk.shader_code.{path.basename(shader.file)}")
        for shader, output, module in zip(resources, gen_files, module_names):
            ninja_writer.build(
                rule=Rule.shader_code,
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
            name=Rule.shader_code_total,
            command=command,
            description="SHADERCODE total generate $out",
        )
        total_output = path.join(Workspace.gen_shader.get_dir(), "shader_code.cc")
        total_module_name = "render.vk.shader_code"
        ninja_writer.build(
            rule=Rule.shader_code_total,
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
    with TestGenNinja.open() as writer:
        pass
    return []


@cached("build_precompile_headers")
def build_precompile_headers(
    header_units: list[HeaderUnit], include_dirs: list[IncludeDir]
):
    Rule = HeaderNinja.Rule
    Phony = HeaderNinja.Phony
    with HeaderNinja.open() as writer:
        writer.rule(
            Rule.precompile,
            Compiler.header_precompile([x.file for x in include_dirs], "$in", "$out"),
            description=f"HEADERUNIT PRECOMPILE $out",
        )
        header_pcm_outputs = []
        for header_unit in header_units:
            output = Compiler.hpcm_file(header_unit.file)
            writer.build(outputs=output, rule=Rule.precompile, inputs=header_unit.file)
            header_pcm_outputs.append(output)
        writer.build(outputs=Phony.header_unit, rule="phony", inputs=header_pcm_outputs)


@cached("build_dep_scan")
def build_dep_scan(
    modules: list[Module],
    sources: list[Source],
    targets: list[Target],
    includes: list[IncludeDir],
):
    Rule = DepScanNinja.Rule
    Phony = DepScanNinja.Phony
    with DepScanNinja.open() as writer:
        command = Script.get_command(
            Script.dep_scan,
            ["-c", "$in"]
            + ["$module_arg"]
            + ["--includes", *[x.file for x in includes]]
            + ["--root_dir", Root.dir],
        )
        writer.rule(
            name=Rule.dep_scan,
            command=command,
            description=f"Dependency scan $in",
        )

        def build_ninja(file: str, module_arg: str):
            writer.build(
                outputs=[
                    DepCtx.dyndep_file(file),
                    DepCtx.header_dep_config_file(file),
                    DepCtx.module_dep_file(file),
                ],
                rule=Rule.dep_scan,
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

        writer.build(
            outputs=Phony.dep_scan,
            rule="phony",
            inputs=[DepCtx.dyndep_file(x.file) for x in sources + targets + modules],
        )


def build_compile(
    modules: list[Module],
    sources: list[Source],
    targets: list[Target],
    includes: list[IncludeDir],
):
    Rule = CompileNinja.Rule
    Phony = CompileNinja.Phony
    with CompileNinja.open() as writer:
        writer.rule(
            name=Rule.precompile,
            command=Compiler.precompile(
                [x.file for x in includes], "$config", "$in", "$out"
            ),
            description=f"PRECOMPILE $out",
        )
        writer.rule(
            name=Rule.compile,
            command=Compiler.compile(
                [x.file for x in includes], "$config", "$in", "$out"
            ),
            description=f"COMPILE $out",
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
                Rule.compile, source.file, source.file, Compiler.obj_file(source.file)
            )
        for target in targets:
            build(
                Rule.compile, target.file, target.file, Compiler.obj_file(target.file)
            )
        for module in modules:
            if module.provide != None:
                build(
                    Rule.precompile,
                    module.file,
                    module.file,
                    Compiler.pcm_file(module.provide),
                )
                build(
                    Rule.compile,
                    module.file,
                    Compiler.pcm_file(module.provide),
                    Compiler.obj_file(module.file),
                )
            else:
                build(
                    Rule.compile,
                    module.file,
                    module.file,
                    Compiler.obj_file(module.file),
                )

        writer.build(
            outputs=Phony.pcm,
            rule="phony",
            inputs=[
                Compiler.pcm_file(
                    module.provide if module.provide != None else str(module.implement)
                )
                for module in modules
            ],
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

    Rule = CompleteDepNinja.Rule
    Phony = CompleteDepNinja.Phony
    with CompleteDepNinja.open() as writer:
        writer.rule(
            name=Rule.complete_dyndep,
            command=Script.get_command(
                Script.complete_dyndep, [Root.dir, "$phony", "$module", "$in", "$out"]
            ),
        )
        for module, files in module_map.items():
            writer.build(
                outputs=Phony.module(module),
                rule="phony",
                inputs=[Compiler.obj_file(file) for file in files],
                order_only=DepCtx.complete_dyndep_module_file(module),
                variables={"dyndep": DepCtx.complete_dyndep_module_file(module)},
            )
            writer.build(
                outputs=DepCtx.complete_dyndep_module_file(module),
                rule=Rule.complete_dyndep,
                inputs=[DepCtx.module_dep_file(file) for file in files],
                variables={
                    "module": f"--module {module}",
                    "phony": Phony.module(module),
                },
            )
        for source in [*sources, *targets]:
            file = source.file
            writer.build(
                outputs=Phony.source(file),
                rule="phony",
                inputs=Compiler.obj_file(file),
                order_only=DepCtx.complete_dyndep_source_file(file),
                variables={"dyndep": DepCtx.complete_dyndep_source_file(file)},
            )
            writer.build(
                outputs=DepCtx.complete_dyndep_source_file(file),
                rule=Rule.complete_dyndep,
                inputs=DepCtx.module_dep_file(file),
                variables={"module": "", "phony": Phony.source(file)},
            )


@cached("build_target")
def build_target(
    targets: list[Target], sources: list[Source], dynamic_libs: list[DylibFile]
):
    Rule = TargetNinja.Rule
    Phony = TargetNinja.Phony
    with TargetNinja.open() as writer:
        writer.rule(
            name=Rule.link,
            command=Script.get_command(Script.link, [Root.dir, "$input", "$out"]),
        )
        writer.rule(
            name=Rule.copy,
            command="cmd.exe /c copy /Y $in $out  > NUL",
            description="COPY dynamic library $out",
        )
        for target in targets:
            writer.build(
                outputs=Compiler.target_file(target.name),
                rule=Rule.link,
                implicit=[
                    CompleteDepNinja.Phony.source(source.file)
                    for source in sources + [target]
                ],
                variables={"input": target.file},
            )
            for dylib in dynamic_libs:
                writer.build(
                    outputs=Compiler.dynamic_dest(dylib.file),
                    rule=Rule.copy,
                    inputs=dylib.file,
                )
            writer.build(
                outputs=Phony.target(target.name),
                rule="phony",
                inputs=[Compiler.target_file(target.name)]
                + [Compiler.dynamic_dest(dylib.file) for dylib in dynamic_libs],
            )


@cached("build_total")
def build_total():
    with NinjaFile.open() as writer:
        writer.subninja(HeaderNinja.get_file())
        writer.subninja(DepScanNinja.get_file())
        writer.subninja(CompileNinja.get_file())
        writer.subninja(CompleteDepNinja.get_file())
        writer.subninja(TargetNinja.get_file())
        writer.subninja(ShaderGenNinja.get_file())
        writer.subninja(TestGenNinja.get_file())


def build_ninja():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root_dir", type=str, dest="root_dir", default="./")
    parser.add_argument(
        "--clangd",
        action="store_true",
        help="provide compile_commands.json and pcm/headerpcm files for clangd",
    )
    args = parser.parse_args()
    Root.set_dir(args.root_dir)
    Workspace.mkdirs()

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
    build_complete_dep(resources.modules, resources.sources, resources.targets)
    build_target(resources.targets, resources.sources, resources.dylib_files)
    build_total()

    if args.clangd:
        update_clangd_pcms(
            resources.header_units,
            resources.modules,
            resources.sources,
            resources.targets,
            resources.include_dirs,
        )
    else:
        print("execute dep_scan...")
        NinjaFile.execute(DepScanNinja.Phony.dep_scan)
        build_compile_commands(
            resources.modules,
            resources.sources,
            resources.targets,
            resources.include_dirs,
            uid=Compiler.current_clangd_uid(),
        )


if __name__ == "__main__":
    build_ninja()
