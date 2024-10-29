import argparse
import textwrap


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("ids", type=str, nargs="+")
    parser.add_argument("output", type=str)
    args = parser.parse_args()

    map_declarations = "\n".join(
        f"extern std::unordered_map<std::string_view, std::function<void()>> {id}_map;"
        for id in args.ids
    )

    test_executions = "\n".join(
        textwrap.dedent(
            f"""\
            std::println("start test {id}");
            for (auto& [name, func] : {id}_map) {{
              std::println("start test {{}}", name);
              func();
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
