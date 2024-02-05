import hashlib
import json
from os import path
import subprocess as sp
import ninja_syntax as ninja

def compute_hash(file_path):
  # 以二进制读取文件内容
  with open(file_path, 'rb') as file:
      # 创建 SHA-256 哈希对象
      sha256 = hashlib.sha256()
      
      # 更新哈希对象
      while chunk := file.read(8192):  # 以 8KB 为单位读取文件
          sha256.update(chunk)
  
  # 返回哈希值的十六进制表示
  return sha256.hexdigest()

build_dir = './build'
output_name = 'hello.exe'
output_path = path.join(build_dir, 'out', output_name)
obj_dir = path.join(build_dir, 'obj')
pcm_dir = path.join(build_dir, 'pcm')
flags = ['C:/Users/18389/msys2/mingw64/bin/clang', '-std=c++23', '-fexperimental-library', '-nostdinc++', '-nostdlib++', '-isystem', 'C:/Users/18389/msys2/mingw64/include/c++/v1', '-Wno-unused-command-line-argument', '-Wno-reserved-module-identifier']
compile_flags = flags + ['-I.\\third_party\include\\', f'-fprebuilt-module-path={pcm_dir}', '-c']
precompile_flags = flags + ['-I.\\third_party\include\\', f'-fprebuilt-module-path={pcm_dir}', '--precompile']
link_flags = flags + [ '-LC:/Users/18389/msys2/mingw64/lib', '-Lthird_party/static_library']
link_librarys = ['c++', 'vulkan-1', 'glfw3dll']

input_files = []
with open('translation_units', 'rt') as f:
  for line in f:
    input_files.append(line.strip())

def exec_scan_deps(files):
    compile_database_path = path.join(build_dir, 'commands_for_scan_dep.json')
    compile_database = []
    for file in files:
      output = file + '.o'
      command = compile_flags + [file, '-o', output, '-IC:/Users/18389/msys64/mingw64/lib/clang/17/include']
      compile_database.append({
          'file': file,
          'directory': path.abspath('.'),
          'arguments': command,
          'output': output,
        })
    with open(compile_database_path, 'wt') as f:
      json.dump(compile_database, f, indent=4)
    result = sp.run(f'clang-scan-deps -format=p1689 -compilation-database {compile_database_path}', stdout=sp.PIPE, check=True)
    def get_info(rule):
      info = {
          'deps': list(map(lambda x: x['logical-name'], rule.get('requires', [])))
        }
      if 'provides' in rule:
        assert(len(rule['provides']) == 1)
        info['module'] = rule['provides'][0]['logical-name']
      return rule['primary-output'][:-2], info
    return {file: info for file, info in map(get_info, json.loads(result.stdout)['rules'])}

def get_scan_deps(files):
  
  cache_path = path.join(build_dir, 'scan_deps.json')
  
  file_hashes = {file: compute_hash(file) for file in files}
  if path.exists(cache_path):
    with open(cache_path, 'rt') as f:
      scan_deps = json.load(f)
  else:
    scan_deps = {}
  need_update_files = list(filter(lambda file: file not in scan_deps or scan_deps[file]['hash'] != file_hashes[file], files))
  updated_infos = exec_scan_deps(need_update_files)
  print(updated_infos)
  for file, updated_info in updated_infos.items():
    updated_info['hash'] = file_hashes[file]
    scan_deps[file] = updated_info
    
  with open(cache_path, 'wt') as f:
    json.dump(scan_deps, f, indent=2)
  
  scan_deps = {file: info for file, info in filter(lambda x: x[0] in files, scan_deps.items())}
  module_file_map = {}
  for file, info in scan_deps.items():
    if 'module' in info:
      module_file_map[info['module']] = file
  for info in scan_deps.values():
    info['redep_num'] = 0
    info['deps'] = list(map(lambda x: module_file_map[x], info['deps']))
  for info in scan_deps.values():
    for dep in info['deps']:
      scan_deps[dep]['redep_num'] += 1

  return scan_deps

dep_info = get_scan_deps(input_files)

obj_files = []

compile_commands = []

def get_pcm_file(file):
  return path.join(pcm_dir, path.relpath(dep_info[file]['module'].replace(':', '-') + '.pcm'))
def get_object_file(file):
  return path.join(obj_dir, path.relpath(file + '.o'))

rule_id = 0
def generate_rule_name():
  global rule_id
  rule_id += 1
  return f'rule_{rule_id}'

with open('build.ninja', 'wt') as f:
  ninja_writer = ninja.Writer(f)
  for file, info in dep_info.items():
    obj_file = get_object_file(file)
    obj_files.append(obj_file)
    dep_pcm_files = list(map(get_pcm_file, info['deps']))

    compile_command = {}
    if 'module' not in info:
      command_obj = compile_flags + [file, '-o', obj_file]
      rule_name = generate_rule_name()
      ninja_writer.rule(rule_name, command_obj)
      ninja_writer.build(outputs=[obj_file], rule=rule_name, inputs=dep_pcm_files+[file])
      compile_command['arguments'] = command_obj
    else:
      pcm_file = get_pcm_file(file)
      command_pcm = precompile_flags + [file, '-o', pcm_file]
      rule_name = generate_rule_name()
      ninja_writer.rule(rule_name, command_pcm)
      ninja_writer.build(outputs=[pcm_file], rule=rule_name, inputs=dep_pcm_files+[file])

      command_obj = compile_flags + [pcm_file, '-o', obj_file]
      rule_name = generate_rule_name()
      ninja_writer.rule(rule_name, command_obj)
      ninja_writer.build(outputs=[obj_file], rule=rule_name, inputs=dep_pcm_files+[pcm_file])
      ninja_writer.build(outputs=[dep_info[file]['module']], rule='phony', inputs=[obj_file])
    
      compile_command['arguments'] = command_pcm

    compile_command.update({
        'directory': path.abspath(path.split(file)[0]),
        'file': path.abspath(file),
      })
    compile_commands.append(compile_command)
  link_command = link_flags + ['-o', output_path] + obj_files + list(map(lambda x: '-l'+x, link_librarys))
  rule_name = generate_rule_name()
  ninja_writer.rule(rule_name, link_command)
  ninja_writer.build(outputs=[output_path], rule=rule_name, inputs=obj_files)
print('ninja generate done')

with open('compile_commands.json', 'wt') as f:
  json.dump(compile_commands, f, indent=2)

print('compile_commands.json done')