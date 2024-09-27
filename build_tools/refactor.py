import public as pub
import os.path as ospath
import yaml
import textwrap
import argparse
import build_ninja

parser = argparse.ArgumentParser()
subparsers = parser.add_subparsers(dest = 'task')

create_task = subparsers.add_parser('create', help = 'create new module')
create_task.add_argument('module_name', type=str)
create_task.add_argument('dir', type=str)
create_task.add_argument('namespace_name', type=str)

remove_task = subparsers.add_parser('remove', help = 'remove existing module')
# remove_task.add_argument()

args = parser.parse_args()



def create_module(
  module_name: str, dir: str, namespace_name: str, source_prefix: str | None = None
):
  resource_meta_path = ospath.join(dir, pub.rsc.config_filename)
  if source_prefix is None:
    source_prefix = module_name.split(".")[-1]
  source_ccm = source_prefix + ".ccm"
  source_cc = source_prefix + ".cc"
  with open(resource_meta_path, "rt") as f:
    content = yaml.safe_load(f)
  content.setdefault(pub.rsc.type_source, []).append(source_ccm)
  content.setdefault(pub.rsc.type_source, []).append(source_cc)
  with open(resource_meta_path, "wt") as f:
    yaml.safe_dump(content, f, sort_keys=False)
  with open(ospath.join(dir, source_ccm), "xt") as f:
    f.write(textwrap.dedent(
      f"""\
      export module {module_name};
      import std;
      import toy;\n
      export namespace {namespace_name} {{\n
      }}
      """))
  with open(ospath.join(dir, source_cc), "xt") as f:
    f.write(textwrap.dedent(
      f"""\
      module {module_name};\n
      namespace {namespace_name} {{\n
      }}
      """))
  build_ninja.build()
  pub.ninja.execute(pub.path.ninja_file, pub.path.get_pcm_file(module_name))


if __name__ == "__main__":
  if args.task == 'create':
    create_module(args.module_name, args.dir, args.namespace_name)
