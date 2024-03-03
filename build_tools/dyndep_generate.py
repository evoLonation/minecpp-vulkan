import subprocess as sp
import argparse
import json
import ninja_syntax as ninja

parser = argparse.ArgumentParser()
parser.add_argument('-c', type=str, dest='source', required=True, help='the abspath of source')
parser.add_argument('-o', type=str, dest='output', required=True)
parser.add_argument('--root_dir', type=str, dest='root_dir', required=True)
parser.add_argument('--compile_flag', type=str, dest='compile_flag', required=True)
args = parser.parse_args()

import public as pub
pub.set_path(args.root_dir, 'whatever')

command = args.compile_flag + f" {args.source} -o {args.source}.o"
result = sp.run(f'clang-scan-deps -format=p1689 -- {command}', stdout=sp.PIPE, check=True)
with open(args.output, 'wt') as f:
  ninja_writer = ninja.Writer(f)
  ninja_writer.variable('ninja_dyndep_version', '1')
  rule = json.loads(result.stdout)['rules'][0]
  module = None
  if 'provides' in rule:
    module = rule['provides'][0]['logical-name']
  ninja_writer.comment(f"[MODULE INFO] {'[EMPTY]' if module is None else module}")

  required_modules = list(map(lambda x: x['logical-name'], rule.get('requires', [])))
  def build_dyndep(output):
    ninja_writer.build(
      outputs=[output],
      rule='dyndep',
      implicit=[pub.path.get_pcm_file(module) for module in required_modules]
    )
  build_dyndep(pub.path.get_obj_file(args.source))
  if module != None:
    build_dyndep(pub.path.get_pcm_file(module))
  