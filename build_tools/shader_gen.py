import argparse
import subprocess as sp


def module2namespace(module: str) -> str:
    return module.replace(".", "::")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    sub_parsers = parser.add_subparsers(dest="task")
    single_parser = sub_parsers.add_parser("single")
    single_parser.add_argument("input", type=str)
    single_parser.add_argument("module", type=str)
    single_parser.add_argument("output", type=str)

    total_parser = sub_parsers.add_parser("total")
    total_parser.add_argument("module", type=str)
    total_parser.add_argument("modules", type=str, nargs="+")
    total_parser.add_argument("output", type=str)

    args = parser.parse_args()

    if args.task == "total":
        import_decl = ""
        pair_decl = ""
        for module in args.modules:
            import_decl += f"import {module};\n"
            pair_decl += f'{{"{module}", std::as_bytes(std::span{{{module2namespace(module)}::shader_code_data}})}},\n'
        code = f"""module {args.module};
            import std;
            {import_decl}
            namespace rd::vk{{
            auto get_shader_code(std::string_view shader) -> std::span<const std::byte> {{
              auto shader_code_map = std::map<std::string_view, std::span<const std::byte>> {{
                {pair_decl}
              }};
              return shader_code_map.at(shader);
            }}
            }}"""
        with open(args.output, "wt") as f:
            f.write(code)
    elif args.task == "single":
        shader_file = args.input
        module = args.module
        shader_codes = sp.run(
            f"glslc {shader_file} --target-env=vulkan1.3 -o -",
            check=True,
            stdout=sp.PIPE,
        ).stdout
        code = f"""export module {module};
                import std;
                export namespace {module2namespace(module)}{{
                  auto shader_code_data = std::array<const unsigned char, {len(shader_codes)}>{{
                    {str(list(shader_codes))[1:-1]}
                  }};
                }}"""
        with open(args.output, "wt") as f:
            f.write(code)
