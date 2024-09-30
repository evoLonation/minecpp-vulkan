import os
import public as pub
import os.path as ospath
import yaml
import textwrap
import argparse
import build_ninja
from typing import Any, Callable
import shutil

parser = argparse.ArgumentParser()
parser.add_argument('--root', type=str, dest='root', required=False, default='./')
subparsers = parser.add_subparsers(dest = 'task', required=True)

create_task = subparsers.add_parser('create', help = 'create new module')
create_task.add_argument('module_name', type=str)
create_task.add_argument('dir', type=str)

add_task = subparsers.add_parser('add_module', help = 'add new module interface resource')
add_task.add_argument('file', type=str)
add_task.add_argument('module_name', type=str)

add_task = subparsers.add_parser('add_impl', help = 'add new module implement resource')
add_task.add_argument('file', type=str)
add_task.add_argument('module_name', type=str)

rename_task = subparsers.add_parser('rename', help = 'rename or move resource')
rename_task.add_argument('old_path', type=str)
rename_task.add_argument('new_path', type=str)

delete_task = subparsers.add_parser('delete', help = 'only delete file from resource config, not delete file itself')
delete_task.add_argument('file', type=str)

delete_task = subparsers.add_parser('add_dir', help = 'add new sub_dir resource (not create directory, just add in config)')
delete_task.add_argument('dir', type=str)

args = parser.parse_args()


def check_config(config_path: str, checker: Callable[[dict[str, list]], dict[str, list] | None]) -> bool:
  if not ospath.exists(config_path):
    new_content = checker({})
  with open(config_path, 'rt') as f:
    old_content = yaml.safe_load(f)
    if isinstance(old_content, dict) and\
      all(isinstance(k, str) and isinstance(v, list) for k, v in old_content.items()):
        new_content = checker(old_content)
    else:
      print(f"type: {type(old_content)}")
      print(f"The old content of config file {config_path} is not a map, ignore")
      new_content = checker({})
  if new_content is not None:
    for key in [key for key, value in new_content.items() if len(value) == 0]:
      new_content.pop(key)
    with open(config_path, 'wt') as f:
      yaml.safe_dump(new_content, f, sort_keys=False)
    return True
  return False

def add_resource(resource_path: str, type: str):
  config_path = ospath.join(ospath.dirname(resource_path), pub.rsc.config_filename)
  # 更新 yml 配置文件
  def changer(content):
    content.setdefault(type, []).append(ospath.basename(resource_path))
    return content
  check_config(config_path, changer)

def add_source_resource(
  filepath: str, content_checker: Callable[[str], bool], new_file_content: str
):
  add_resource(filepath, pub.rsc.type_source)
  
  # 检查文件
  if ospath.exists(filepath):
    with open(filepath, 'rt') as f:
      old_content = f.read()
      if old_content.strip() != '':
        if not content_checker(old_content):
          raise RuntimeError(f"The file {filepath} already has content and not match with the resource type")
        else:
          print(f"The file {filepath} already exists, not change")
          return
  # 修改文件
  with open(filepath, "at") as f:
    f.write(new_file_content)

def add_module_resource(
  filepath: str, module_name: str
):
  if not filepath.endswith('.ccm'):
    raise RuntimeError("The file suffix is not .ccm")
  def content_checker(content: str):
    try:
      return content.strip()[len('export module'):].strip() != module_name
    except:
      return False
  add_source_resource(filepath, content_checker, textwrap.dedent(
    f"""\
    export module {module_name};
    import std;
    import toy;
    """))

def add_impl_resource(
  filepath: str, module_name: str
):
  if not filepath.endswith('.cc'):
    raise RuntimeError("The file suffix is not .cc")
  def content_checker(content: str):
    try:
      return content.strip()[len('module'):].strip() != module_name
    except:
      return False
  add_source_resource(filepath, content_checker, textwrap.dedent(
    f"""\
    module {module_name};
    """))
  
def create_module(
  module_name: str, dir: str, source_prefix: str | None = None
):
  if source_prefix is None:
    source_prefix = module_name.split(".")[-1]
  source_ccm = source_prefix + ".ccm"
  source_cc = source_prefix + ".cc"
  add_module_resource(ospath.join(dir, source_ccm), module_name)
  add_impl_resource(ospath.join(dir, source_cc), module_name)
  
def rename_resource(
  old_path: str, new_path: str
):
  if ospath.exists(old_path):
    shutil.move(old_path, new_path)
    print(f"The rename is not happend before, so rename first")
  elif ospath.exists(new_path):
    print(f"The rename is already happend, just need to change config")
  else:
    raise RuntimeError("The file is not exists either on old_path and new_path")
  old_filename = ospath.basename(old_path)
  new_filename = ospath.basename(new_path)
  old_config_path = ospath.join(ospath.dirname(old_path), pub.rsc.config_filename)

  is_move_dir: bool = ospath.dirname(old_path) != ospath.dirname(new_path)

  if not is_move_dir:
    print(f"The file {old_path} just rename to {new_filename}")
    def config_changer(content: dict[str, list]):
      changed = False
      for resources in content.values():
        for i, resource in enumerate(resources):
          if resource == old_filename:
            changed = True
            resources[i] = new_filename
      return content if changed else None 
    if check_config(old_config_path, config_changer):
      print(f"change the resource name in config")
    else:
      print(f"do nothing")
  else:
    print(f"The file {old_path} move to new path {new_path} with different dir")
    moved_types: set[str] = set()
    def config_checker(content: dict[str, list]):
      changed = False
      for type, resources in content.items():
        kept = [resource for resource in resources if resource != old_filename]
        if len(kept) < len(resources):
          moved_types.add(type)
          content[type] = kept
          changed = True
      return content if changed else None
    check_config(old_config_path, config_checker)
    if len(moved_types) == 0:
      print(f"do nothing")
      return
    new_config_path = ospath.join(ospath.dirname(new_path), pub.rsc.config_filename)
    def new_config_changer(content: dict[str, list]):
      for type in moved_types:
        content.setdefault(type, []).append(new_filename)
      return content
    assert check_config(new_config_path, new_config_changer)
    print(f"Move the resource name in from {old_config_path} to {new_config_path}")
  
def delete_resource(filepath: str):
  config_path = ospath.join(ospath.dirname(filepath), pub.rsc.config_filename)
  filename = ospath.basename(filepath)
  def config_changer(content: dict[str, list]):
    changed = False
    for type, resources in content.items():
      try:
        resources.index(filename)
      except:
        continue
      content[type] = [resource for resource in resources if resource != filename]
      changed = True
    return content if changed else None
  check_config(config_path, config_changer)

def add_directory(dir: str):
  add_resource(dir, pub.rsc.type_sub_dir)

if __name__ == "__main__":
  pub.set_path(args.root, 'test')
  if args.task == 'create':
    create_module(args.module_name, args.dir)
    build_ninja.precompile_by_module(args.module_name)
  elif args.task == 'add_module':
    add_module_resource(args.file, args.module_name)
    build_ninja.precompile_by_file(args.file)
  elif args.task == 'add_impl':
    add_impl_resource(args.file, args.module_name)
    build_ninja.build_ninja()
  elif args.task == 'rename':
    rename_resource(args.old_path, args.new_path)
    build_ninja.build_ninja()
  elif args.task == 'delete':
    delete_resource(args.file)
    build_ninja.build_ninja()
  elif args.task == 'add_dir':
    add_directory(args.dir)
