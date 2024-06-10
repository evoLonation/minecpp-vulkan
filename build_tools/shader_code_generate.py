import os.path as ospath
import argparse
import subprocess as sp 

parser = argparse.ArgumentParser()
parser.add_argument('--task', type=str, dest='task', required=True)
parser.add_argument('-c', type=str, dest='input', nargs='+', required=True)
parser.add_argument('-o', type=str, dest='output', required=False)
args = parser.parse_args()

def get_shader_identify(shader_file):
  return ospath.basename(shader_file).replace('.', '_')

if args.task == 'total':
  import_decl = ''
  pair_decl = ''
  for shader_file in args.input:
    import_decl += f'import shader_code.{get_shader_identify(shader_file)};\n'
    pair_decl += f'{{"{ospath.basename(shader_file)}", std::as_bytes(std::span{{shader_code::{get_shader_identify(shader_file)}::shader_code_data}})}},\n'
  code = f'''module render.vk.shader_code;
          import std;
          {import_decl}
          namespace rd::vk{{
          auto get_shader_code(std::string_view shader) -> std::span<const std::byte> {{
            auto shader_code_map = std::map<std::string_view, std::span<const std::byte>> {{
              {pair_decl}
            }};
            return shader_code_map.at(shader);
          }}
          }}'''
  with open(args.output, 'wt') as f:
    f.write(code)
elif args.task == 'single':
  shader_file = args.input[0]
  shader_codes = sp.run(f'glslc {shader_file} -o -', check=True, stdout=sp.PIPE).stdout
  code =  f'''export module shader_code.{get_shader_identify(shader_file)};
              import std;
              export namespace shader_code::{get_shader_identify(shader_file)}{{
                auto shader_code_data = std::array<const unsigned char, {len(shader_codes)}>{{
                  {str(list(shader_codes))[1:-1]}
                }};
              }}'''
  with open(args.output, 'wt') as f:
    f.write(code)
elif args.task == 'module':
  shader_file = args.input[0]
  print(f"shader_code.{get_shader_identify(shader_file)}")