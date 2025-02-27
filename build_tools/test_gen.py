import argparse
import textwrap


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("ids", type=str, nargs="+")
    parser.add_argument("output", type=str)
    args = parser.parse_args()

    map_declarations = "\n".join(
        f"extern std::vector<std::pair<std::string_view, void(*)()>> {id}_list;"
        for id in args.ids
    )

    test_executions = "\n".join(
        textwrap.dedent(
            f"""\
            std::println("start test {id}");
            for (auto& [name, func] : {id}_list) {{
              std::println("start test {{}}", name);
              try {{
                func();
              }} catch (const std::exception& e) {{
                std::println("\033[31mTEST {{}} FAILED!\033[0m reason:\\n{{}}", name, e.what());
                continue;
              }}
              std::println("\033[32mTEST {{}} SUCCESS! \033[0m", name);
            }}
            """
        )
        for id in args.ids
    )

    template = textwrap.dedent(
        f"""\
        import std;

        {map_declarations}

        int main() {{
          {test_executions}
          return 0;
        }}
        """
    )

    with open(args.output, "wt") as f:
        f.write(template)
