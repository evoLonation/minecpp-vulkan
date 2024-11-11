from os import path
import textwrap
from build_ninja import build_ninja
from clangd_remove_invalid import (
    get_compile_commands_content,
    write_compile_commands_json,
)
from public import Root
from resources import Module, Resources, get_config_file, touch_config_file
import argparse


def add_module_interface(file: str, module: str, new_file: bool) -> bool:
    config_file = touch_config_file(path.dirname(file))
    resources = Resources.from_file(config_file)
    resources.modules.append(Module(file=path.basename(file), provide=module))
    resources.to_file(config_file)
    if new_file:
        assert path.exists(file), f"file not found: {file}"
        with open(file, "rt") as f:
            assert f.read().strip() == ""
        with open(file, "wt") as f:
            template = textwrap.dedent(
                f"""\
            module;
            #include <toy.h>
            export module {module};
            
            import std;
            import toy;

            export namespace {module.split('.')[0]} {{
                
            }}
            """
            )
            f.write(template)
    return True


def add_module_implement(file: str, module: str, new_file: bool) -> bool:
    config_file = touch_config_file(path.dirname(file))
    resources = Resources.from_file(config_file)
    resources.modules.append(Module(file=path.basename(file), implement=module))
    resources.to_file(config_file)
    if new_file:
        assert path.exists(file), f"file not found: {file}"
        with open(file, "rt") as f:
            assert f.read().strip() == ""
        with open(file, "wt") as f:
            template = textwrap.dedent(
                f"""\
            module;
            #include <toy.h>
            module {module};
            
            namespace {module.split('.')[0]} {{
                
            }}"""
            )
            f.write(template)
    return True


def rename_resource(old_path: str, new_path: str) -> bool:
    old_config_file = get_config_file(path.dirname(old_path))
    old_resources = Resources.from_file(old_config_file)
    if path.normpath(path.dirname(old_path)) == path.normpath(path.dirname(new_path)):
        renamed = None
        for rs in old_resources.get_all_resources().values():
            renameds = [r for r in rs if r.file == path.basename(old_path)]
            if len(renameds) > 0:
                assert len(renameds) == 1
                assert renamed is None
                renamed = renameds[0]
        if renamed is None:
            print(f"resource not found in config file, do nothing: {old_path}")
            return False
        renamed.file = path.basename(new_path)
        old_resources.to_file(old_config_file)
        return True
    moved = None
    for rs in old_resources.get_all_resources().values():
        moveds = [r for r in rs if r.file == path.basename(old_path)]
        if len(moveds) > 0:
            kepts = [r for r in rs if r.file != path.basename(old_path)]
            rs.clear()
            rs.extend(kepts)
            assert len(moveds) == 1
            assert moved is None
            moved = moveds[0]
    if moved is None:
        print(f"resource not found in config file, do nothing: {old_path}")
        return False

    assert moved is not None, f"resource not found: {old_path}"

    moved.file = path.basename(new_path)

    new_config_file = touch_config_file(path.dirname(new_path))
    new_resources = Resources.from_file(new_config_file)
    new_resources.get_by_type(type(moved)).append(moved)

    old_resources.to_file(old_config_file)
    new_resources.to_file(new_config_file)
    return True


def delete_resource(file: str) -> bool:
    config_file = get_config_file(path.dirname(file))
    resources = Resources.from_file(config_file)
    moved = False
    for rs in resources.get_all_resources().values():
        kepts = [r for r in rs if r.file != path.basename(file)]
        if len(kepts) != len(rs):
            rs.clear()
            rs.extend(kepts)
            assert not moved
            moved = True

    if not moved:
        print(f"resource not found in config file, do nothing: {file}")
        return False

    resources.to_file(config_file)
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="refactor resources (only effect resource config file, not file itself)"
    )
    parser.add_argument("root_dir", type=str)
    subparsers = parser.add_subparsers(dest="task", required=True)

    add_task = subparsers.add_parser(
        "add_interface",
        help="add new module interface resource",
    )
    add_task.add_argument("file", type=str)
    add_task.add_argument("module_name", type=str)
    add_task.add_argument(
        "--new-file",
        action="store_true",
        help="generate interface file template content",
    )

    add_task = subparsers.add_parser(
        "add_impl",
        help="add new module implement resource",
    )
    add_task.add_argument("file", type=str)
    add_task.add_argument("module_name", type=str)
    add_task.add_argument(
        "--new-file",
        action="store_true",
        help="generate implement file template content",
    )

    rename_task = subparsers.add_parser(
        "rename",
        help="rename or move resource",
    )
    rename_task.add_argument("old_path", type=str)
    rename_task.add_argument("new_path", type=str)

    delete_task = subparsers.add_parser(
        "delete",
        help="delete resource",
    )
    delete_task.add_argument("file", type=str)

    args = parser.parse_args()

    Root.set_dir(args.root_dir)

    success = False
    if args.task == "add_interface":
        success = add_module_interface(args.file, args.module_name, args.new_file)
    elif args.task == "add_impl":
        success = add_module_implement(args.file, args.module_name, args.new_file)
    elif args.task == "rename":
        success = rename_resource(args.old_path, args.new_path)
    elif args.task == "delete":
        success = delete_resource(args.file)
    if success:
        build_ninja(True)
        write_compile_commands_json(get_compile_commands_content())
