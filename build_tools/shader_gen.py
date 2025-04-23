import argparse
import os
import subprocess as sp
import tool
from public import Root

def module2namespace(module: str) -> str:
    return module.replace(".", "::")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root_dir", type=str, dest="root_dir", default="./")
    sub_parsers = parser.add_subparsers(dest="task")
    single_parser = sub_parsers.add_parser("single")
    single_parser.add_argument("input", type=str)
    single_parser.add_argument("module", type=str)
    single_parser.add_argument("output", type=str)

    total_parser = sub_parsers.add_parser("total")
    total_parser.add_argument("module", type=str)
    total_parser.add_argument("output", type=str)
    total_parser.add_argument("--modules", type=str, nargs="+", required=True)
    total_parser.add_argument(
        "--names", type=str, nargs="+", help="shader names in map", required=True
    )

    args = parser.parse_args()
    Root.set_dir(args.root_dir)

    if args.task == "total":
        import_decl = ""
        pair_decl = ""
        for module, name in zip(args.modules, args.names):
            import_decl += f"import {module};\n"
            pair_decl += f'{{"{name}", std::as_bytes(std::span{{{module2namespace(module)}::shader_code_data}})}},\n'
        code = f"""module {args.module};
            import std;
            import toy;
            {import_decl}
            namespace rd{{
            auto get_shader_code(std::string_view shader) -> std::span<const std::byte> {{
              try{{
                auto shader_code_map = std::map<std::string_view, std::span<const std::byte>> {{
                  {pair_decl}
                }};
                return shader_code_map.at(shader);
              }} catch(std::exception const& e){{
                toy::throwf("shader not found: {{}}", shader);
              }}
            }}
            }}"""
        with open(args.output, "wt") as f:
            f.write(code)
    elif args.task == "single":
        shader_file = args.input
        module = args.module
        shader_codes = tool.run_command(
            # todo: can configure glslc path and vulkan version
            f"glslc {shader_file} --target-env=vulkan1.2 -o -",
            errlog=f"Failed to compile shader {shader_file}",
            output=tool.OutputMode.RAW,
        )
        code = f"""export module {module};
                import std;
                export namespace {module2namespace(module)}{{
                  auto shader_code_data = std::array<const unsigned char, {len(shader_codes)}>{{
                    {str(list(shader_codes))[1:-1]}
                  }};
                }}"""
        with open(args.output, "wt") as f:
            f.write(code)
