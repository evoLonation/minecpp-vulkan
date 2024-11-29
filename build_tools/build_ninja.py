import argparse
import os.path as path
from clangd_remove_invalid import (
    get_compile_commands_content,
    write_compile_commands_json,
)
from cache import cached, set_enable_avoid_call
from public import (
    Compiler,
    DepCtx,
    HeaderNinja,
    NinjaFile,
    RemoveInvalidNinja,
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


def get_target_sources(targets: list[Target]) -> list[Source]:
    return [Source(file=target.file, targets=[target.file]) for target in targets]


@cached
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
            description="SHADERCODE SINGLE GEN $out",
        )
        gen_files = []
        module_names = []
        shader_names = []
        for shader in resources:
            gen_files.append(
                path.join(
                    Workspace.gen_shader.get_dir(), Root.relpath(shader.file) + ".ccm"
                )
            )
            module_names.append(f"render.shader_code.{path.basename(shader.file)}")
            shader_names.append(path.basename(shader.file))
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
            ["total", "$module", "$out", "--modules", "$modules", "--names", "$names"],
        )
        # 生成 total shader_code
        ninja_writer.rule(
            name=Rule.shader_code_total,
            command=command,
            description="SHADERCODE TOTAL GEN $out",
        )
        total_output = path.join(Workspace.gen_shader.get_dir(), "shader_code.cc")
        total_module_name = "render.shader_code"
        ninja_writer.build(
            rule=Rule.shader_code_total,
            outputs=total_output,
            variables={
                "module": total_module_name,
                "modules": " ".join([x for x in module_names]),
                "names": " ".join([x for x in shader_names]),
            },
        )
        module_resources.append(Module(file=total_output, implement=total_module_name))
    return module_resources


@cached
def build_gen_test(resources: list[Test]) -> tuple[list[Source], list[Target]]:
    sources = []
    targets = []
    with TestGenNinja.open() as writer:
        writer.rule(
            name=TestGenNinja.Rule.test_main,
            command=Script.get_command(Script.test_gen, ["$ids", "$out"]),
            description="TEST MAIN GEN $out",
        )
        for test in resources:
            output = path.join(Workspace.gen_test.get_dir(), test.get_id() + ".cc")
            writer.build(
                outputs=output,
                rule=TestGenNinja.Rule.test_main,
                variables={"ids": test.get_id()},
            )
            targets.append(Target(file=output, name=test.get_id()))
            sources.append(
                Source(
                    file=test.file,
                    targets=[output],
                    macros=[("TEST_IDENTITY", test.get_id())],
                )
            )
    return sources, targets


@cached
def build_precompile_headers(
    header_units: list[HeaderUnit], include_dirs: list[IncludeDir], clangd: bool
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
            writer.build(
                outputs=output,
                rule=Rule.precompile,
                inputs=header_unit.file,
                order_only=[RemoveInvalidNinja.get_output()] if clangd else [],
            )
            header_pcm_outputs.append(output)
        writer.build(outputs=Phony.header_unit, rule="phony", inputs=header_pcm_outputs)


@cached
def build_dep_scan(
    modules: list[Module],
    sources: list[Source],
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
            description=f"DEPENDENCY SCAN $in",
            # if output is not change, it will still update dependency state (looks like output is change)
            restat=True,
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
        for module in modules:
            if module.implement != None:
                build_ninja(module.file, f"--implement {module.implement}")
            else:
                build_ninja(module.file, f"--provide {module.provide}")

        writer.build(
            outputs=Phony.dep_scan,
            rule="phony",
            inputs=[DepCtx.dyndep_file(x.file) for x in sources + modules],
        )


def build_compile(
    modules: list[Module],
    sources: list[Source],
    includes: list[IncludeDir],
    clangd: bool,
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
                [x.file for x in includes], "$config", "$in", "$out", "$extra"
            ),
            description=f"COMPILE SOURCE $out",
        )
        writer.rule(
            name=Rule.compile_pcm,
            command=Compiler.compile(
                [x.file for x in includes], "$config", "$in", "$out"
            ),
            description=f"COMPILE MODULE $out",
        )

        def build(
            rule: str,
            source: str,
            input: str,
            output: str,
            output_pcm: bool = False,
            macros: list[tuple[str, str]] = [],
        ):
            writer.build(
                outputs=output,
                rule=rule,
                inputs=input,
                order_only=[DepCtx.dyndep_file(source)]
                + ([RemoveInvalidNinja.get_output()] if clangd and output_pcm else []),
                variables={
                    "dyndep": DepCtx.dyndep_file(source),
                    "config": DepCtx.header_dep_config_file(source),
                    **(
                        {"extra": Compiler.get_macro_flag(macros)}
                        if len(macros) > 0
                        else {}
                    ),
                },
            )

        for source in sources:
            build(
                Rule.compile,
                source.file,
                source.file,
                Compiler.obj_file(source.file),
                macros=source.macros,
            )
        for module in modules:
            if module.provide != None:
                build(
                    Rule.precompile,
                    module.file,
                    module.file,
                    Compiler.pcm_file(module.provide),
                    True,
                )
                build(
                    Rule.compile_pcm,
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


@cached
def build_remove_invalid():
    Ninja = RemoveInvalidNinja
    Rule = Ninja.Rule
    with Ninja.open() as writer:
        writer.rule(
            name=Rule.remove_invalid,
            command=Script.get_command(
                Script.clangd_remove_invaid, ["$root_dir", "$out"]
            ),
            description=f"REMOVE INVALID PCM FILES",
        )
        writer.build(
            outputs=Ninja.get_output(),
            rule=Rule.remove_invalid,
            inputs=[],
            implicit=[DepScanNinja.Phony.dep_scan],
            variables={"root_dir": Root.dir},
        )


# a module phony A will build all relative files needed by module A (whole dependency tree)
@cached
def build_complete_dep(modules: list[Module], sources: list[Source]):
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
            description="COMPLETE DYNDEP $out",
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
        for source in sources + modules:
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


@cached
def build_target(
    targets: list[Target], sources: list[Source], dynamic_libs: list[DylibFile]
):
    Rule = TargetNinja.Rule
    Phony = TargetNinja.Phony
    with TargetNinja.open() as writer:
        writer.rule(
            name=Rule.link,
            command=Script.get_command(Script.link, [Root.dir, "$input", "$out"]),
            description="LINK $out",
        )
        writer.rule(
            name=Rule.copy,
            command="cmd.exe /c copy /Y $in $out  > NUL",
            description="COPY DYLIB $out",
        )
        for dylib in dynamic_libs:
            writer.build(
                outputs=Compiler.dynamic_dest(dylib.file),
                rule=Rule.copy,
                inputs=dylib.file,
            )
        for target in targets:
            writer.build(
                outputs=Compiler.target_file(target.name),
                rule=Rule.link,
                implicit=[
                    CompleteDepNinja.Phony.source(source.file)
                    for source in sources
                    if source.needed_by(target)
                ],
                variables={"input": target.file},
            )
            writer.build(
                outputs=Phony.target(target.name),
                rule="phony",
                inputs=[Compiler.target_file(target.name)]
                + [Compiler.dynamic_dest(dylib.file) for dylib in dynamic_libs],
            )


@cached
def build_total(clangd: bool):
    with NinjaFile.open() as writer:
        writer.subninja(HeaderNinja.get_file())
        writer.subninja(DepScanNinja.get_file())
        writer.subninja(CompileNinja.get_file())
        writer.subninja(CompleteDepNinja.get_file())
        writer.subninja(TargetNinja.get_file())
        writer.subninja(ShaderGenNinja.get_file())
        writer.subninja(TestGenNinja.get_file())
        if clangd:
            writer.subninja(RemoveInvalidNinja.get_file())

@cached
def generate_modules(modules: list[Module]):
    with open(path.join(Workspace.build.get_dir(), "modules.txt"), "wt") as f:
        for module in modules:
            f.write(f"{module.implement if module.implement else module.provide}\n")


def build_ninja(clangd: bool):
    resources = get_file_resources()
    # print(f"resources: {resources}")

    resources.modules.extend(build_gen_shader(resources.shaders))
    test_sources, test_targets = build_gen_test(resources.tests)
    resources.sources.extend(test_sources)
    resources.targets.extend(test_targets)
    resources.sources.extend(get_target_sources(resources.targets))
    save_resources(resources)

    build_precompile_headers(resources.header_units, resources.include_dirs, clangd)
    build_dep_scan(resources.modules, resources.sources, resources.include_dirs)
    build_compile(resources.modules, resources.sources, resources.include_dirs, clangd)
    build_complete_dep(resources.modules, resources.sources)
    build_target(resources.targets, resources.sources, resources.dylib_files)
    if clangd:
        build_remove_invalid()
        generate_modules(resources.modules)
    build_total(clangd)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root_dir", type=str, dest="root_dir", default="./")
    parser.add_argument(
        "--clangd",
        action="store_true",
        help="provide compile_commands.json for clangd and remove invalid pcms before build pcms to ensure clangd not crash",
    )
    parser.add_argument("--disable-cache", action="store_true")
    args = parser.parse_args()

    args.clangd = True
    # args.disable_cache = True

    set_enable_avoid_call(not args.disable_cache)
    Root.set_dir(args.root_dir)
    Workspace.mkdirs()

    build_ninja(args.clangd)

    if args.clangd:
        write_compile_commands_json(get_compile_commands_content())
