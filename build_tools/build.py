import hashlib
import json
import os.path as ospath
import os
import subprocess as sp
import ninja_syntax as ninja
import tempfile

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

root_dir = './'
output_name = 'hello.exe'

build_dir = ospath.join(root_dir, 'build')
os.makedirs(build_dir, exist_ok=True)
ninja_build_file = ospath.join(build_dir, 'build.ninja')
output_file = ospath.join(build_dir, 'out', output_name)
obj_dir = ospath.join(build_dir, 'obj')
pcm_dir = ospath.join(build_dir, 'pcm')
ninja_file = ospath.join(build_dir, 'build.ninja')

flags = ['C:/Users/18389/msys2/mingw64/bin/clang', '-std=c++23', '-fexperimental-library', '-nostdinc++', '-nostdlib++', '-isystem', 'C:/Users/18389/msys2/mingw64/include/c++/v1', '-Wno-unused-command-line-argument', '-Wno-reserved-module-identifier']
include_dirs = [
    ospath.join(root_dir, 'third_party\include')
  ]
include_flags = list(map(lambda x: '-I'+ospath.abspath(x), include_dirs))
compile_flags = flags + include_flags +[f'-fprebuilt-module-path={ospath.abspath(pcm_dir)}', '-c']
precompile_flags = flags + include_flags + [f'-fprebuilt-module-path={ospath.abspath(pcm_dir)}', '--precompile']

link_dirs = [
    ospath.join(root_dir, 'third_party\static_library')
  ]
link_flags = flags + [ '-LC:/Users/18389/msys2/mingw64/lib' ] + list(map(lambda x: '-L'+ospath.abspath(x), link_dirs))
link_librarys = ['c++', 'vulkan-1', 'glfw3dll']

# 所有文件路径都是相对于 root_dir 的规范化相对路径
input_files = []

dir_stack = [root_dir]
while len(dir_stack) != 0:
  dir = dir_stack.pop()
  with open(ospath.join(dir, 'sources'), 'rt') as f:
    for line in f:
      path = ospath.join(dir, line.strip())
      if ospath.isdir(path):
        dir_stack.append(path)
      else:
        input_files.append(ospath.normpath(ospath.relpath(path, root_dir)))

def exec_scan_deps(files):
    with tempfile.NamedTemporaryFile('wt') as f:
    # compile_database_path = ospath.join(build_dir, 'commands_for_scan_dep.json')
      compile_database = []
      for file in files:
        output = file + '.o'
        command = compile_flags + [file, '-o', output, '-IC:/Users/18389/msys64/mingw64/lib/clang/17/include']
        compile_database.append({
            'file': file,
            'directory': ospath.abspath('.'),
            'arguments': command,
            'output': output,
          })
    # with open(compile_database_path, 'wt') as f:
      json.dump(compile_database, f, indent=4)
      f.flush()
      result = sp.run(f'clang-scan-deps -format=p1689 -compilation-database {f.name}', stdout=sp.PIPE, check=True)
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
  
  cache_path = ospath.join(build_dir, 'scan_deps.json')
  
  file_hashes = {file: compute_hash(file) for file in files}
  if ospath.exists(cache_path):
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
  for file, info in scan_deps.items():
    info['redep_num'] = 0
    for dep in info['deps']:
      if dep not in module_file_map:
        raise RuntimeError(f'not found module "{dep}" for file {file}')
    info['deps'] = list(map(lambda x: module_file_map[x], info['deps']))
  for info in scan_deps.values():
    for dep in info['deps']:
      scan_deps[dep]['redep_num'] += 1

  return scan_deps

dep_info = get_scan_deps(input_files)

obj_files = []

compile_commands = []

def get_pcm_file(file):
  return ospath.abspath(ospath.join(pcm_dir, ospath.relpath(dep_info[file]['module'].replace(':', '-') + '.pcm')))
def get_object_file(file):
  return ospath.abspath(ospath.join(obj_dir, ospath.relpath(file + '.o')))

rule_id = 0
def generate_rule_name():
  global rule_id
  rule_id += 1
  return f'rule_{rule_id}'

with open(ninja_build_file, 'wt') as f:
  ninja_writer = ninja.Writer(f)
  for file, info in dep_info.items():
    abs_file = ospath.abspath(file)
    obj_file = get_object_file(file)
    obj_files.append(obj_file)
    dep_pcm_files = list(map(get_pcm_file, info['deps']))

    compile_command = {}
    if 'module' not in info:
      command_obj = compile_flags + [abs_file, '-o', obj_file]
      rule_name = generate_rule_name()
      ninja_writer.rule(rule_name, command_obj, description=f'COMPILE {ospath.relpath(obj_file, root_dir)}')
      ninja_writer.build(outputs=[obj_file], rule=rule_name, inputs=dep_pcm_files+[abs_file])
      compile_command['arguments'] = command_obj
    else:
      pcm_file = get_pcm_file(file)
      command_pcm = precompile_flags + [abs_file, '-o', pcm_file]
      rule_name = generate_rule_name()
      ninja_writer.rule(rule_name, command_pcm, description=f'PRECOMPILE {ospath.relpath(pcm_file, root_dir)}')
      ninja_writer.build(outputs=[pcm_file], rule=rule_name, inputs=dep_pcm_files+[abs_file])

      command_obj = compile_flags + [pcm_file, '-o', obj_file]
      rule_name = generate_rule_name()
      ninja_writer.rule(rule_name, command_obj, description=f'COMPILE {ospath.relpath(obj_file, root_dir)}')
      ninja_writer.build(outputs=[obj_file], rule=rule_name, inputs=dep_pcm_files+[pcm_file])
      ninja_writer.build(outputs=[dep_info[file]['module']], rule='phony', inputs=[obj_file])
    
      compile_command['arguments'] = command_pcm

    compile_command.update({
        'directory': ospath.abspath(ospath.split(file)[0]),
        'file': ospath.abspath(file),
      })
    compile_commands.append(compile_command)
  link_command = link_flags + ['-o', ospath.abspath(output_file)] + obj_files + list(map(lambda x: '-l'+x, link_librarys))
  rule_name = generate_rule_name()
  ninja_writer.rule(rule_name, link_command, description=f'LINK {ospath.relpath(output_file)}')
  ninja_writer.build(outputs=[ospath.abspath(output_file)], rule=rule_name, inputs=obj_files)
print('ninja generate done')

with open('compile_commands.json', 'wt') as f:
  json.dump(compile_commands, f, indent=2)

print('compile_commands.json done')