import os
import hashlib
import argparse
import json
from os import path
import subprocess as sp
from concurrent.futures import ThreadPoolExecutor, as_completed
import threading
from queue import Queue



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
flags = ['C:/Users/18389/msys2/mingw64/bin/clang', '-std=c++23', '-fexperimental-library', '-nostdinc++', '-nostdlib++', '-isystem', 'C:/Users/18389/msys2/mingw64/include/c++/v1']
compile_flags = flags + ['-I.\\third_party\include\\', f'-fprebuilt-module-path={pcm_dir}', '-c']
precompile_flags = flags + ['-I.\\third_party\include\\', f'-fprebuilt-module-path={pcm_dir}', '--precompile']
link_flags = flags + [ '-LC:/Users/18389/msys2/mingw64/lib', '-Lthird_party/static_library']
link_librarys = ['c++', 'vulkan-1', 'glfw3dll']

input_files = []
with open('translation_units', 'rt') as f:
  for line in f:
    input_files.append(line.strip())

def exec_scan_deps(files):
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
    with open(path.join(build_dir, 'commands_for_scan_dep.json'), 'wt') as f:
      json.dump(compile_database, f, indent=4)
    result = sp.run('clang-scan-deps -format=p1689 -compilation-database commands_for_scan_dep.json', stdout=sp.PIPE, check=True)
    def get_info(rule):
      info = {
          'deps': list(map(lambda x: x['logical-name'], rule.get('requires', [])))
        }
      if 'provides' in rule:
        assert(len(rule['provides']) == 1)
        info['module'] = rule['provides'][0]['logical-name']
      return rule['primary-output'][:-2], info
    return {file: info for file, info in map(get_info, json.loads(result.stdout)['rules'])}

# info = {
#     'module': 'A', optional
#     'deps': ['B'], module list
#     'hash': 'xxx',
#   }
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
  for file, updated_info in updated_infos.items():
    updated_info['hash'] = file_hashes[file]
    scan_deps[file] = updated_info
    
  with open(cache_path, 'wt') as f:
    json.dump(scan_deps, f, indent=2)
  
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

class Edge:
  def __init__(self):
    self.command = None
    self.output = None
    self.next = []
    self.prev = 0
    self.lock = threading.Lock()
    # 前置节点是否都已缓存
    self.prev_all_cached = True
  def __repr__(self) -> str:
    return str({
      # 'command': self.command,
      'next': self.next,
      'prev': self.prev,  
    })

edges = {}
obj_files = []

for file, info in dep_info.items():
  obj_file = path.join(obj_dir, path.relpath(file + '.o'))
  obj_files.append(obj_file)
  edge = edges.setdefault(file, Edge())
  edge.prev = len(info['deps'])

  for dep in info['deps']:
    dep_edge = edges.setdefault(dep, Edge())
    dep_edge.next.append(file)
  if info['redep_num'] == 0:
    edge.command = compile_flags + [file, '-o', obj_file]
    edge.output = obj_file
  else:
    pcm_file = path.join(pcm_dir, path.relpath(info['module'].replace(':', '-') + '.pcm'))
    command_pcm = precompile_flags + [file, '-o', pcm_file]
    command_obj = compile_flags + [pcm_file, '-o', obj_file]
    edge.command = command_pcm
    edge.next += [file+'@output']
    edge.output = pcm_file
    obj_edge = Edge()
    obj_edge.command = command_obj
    obj_edge.prev = 1
    obj_edge.output = obj_file
    edges[file+'@output'] = obj_edge

print(edges)

future_queue = Queue()

def cache_build(input: str):
  pass
def check_change(input: str):
  pass

with ThreadPoolExecutor(max_workers=20) as executor:  # 创建一个最大容纳数量为5的线程池
  def execute(edge: Edge):
    os.makedirs(path.split(edge.output)[0], exist_ok=True)
    cached = False
    # if edge.prev_all_cached and not check_change():
    #   cached = True
    if not cached:
      sp.run(edge.command, check=True)
    for next in edge.next:
      next_edge = edges[next]
      with next_edge.lock:
        if not cached:
          next_edge.prev_all_cached = False
        assert(next_edge.prev > 0)
        next_edge.prev -= 1
        if next_edge.prev == 0:
          future_queue.put(executor.submit(execute, next_edge))
  for file, edge in edges.items():
    if edge.prev == 0:
      future_queue.put(executor.submit(execute, edge))  

  for _ in range(len(edges)):
    future_queue.get().result()

print('all compile done')

os.makedirs(path.split(output_path)[0], exist_ok=True)
link_command = link_flags + ['-o', output_path] + obj_files + list(map(lambda x: '-l'+x, link_librarys))
sp.run(link_command, check=True)

print('link done')


# if args.task == 'build':
#   filepath = './main.cc'
#   command = f"clang {filepath} -o main.exe   -L./third_party/static_library/  -lc++ -lvulkan-1 -lglfw3dll -include-pch tool.h.pch -include-pch vulkan_config.h.pch -fimplicit-modules -fimplicit-module-maps" + flags
#   os.system(command)
#   compile_command = {
#     'directory': path.split(path.abspath(filepath))[0],
#     'command': command,
#     'file': path.abspath(filepath)
#   }
#   with open('./compile_commands.json', 'wt') as f:
#     json.dump([compile_command], f, indent=2)

# elif args.task == 'pch':
#   os.system("clang++ -x c++-header tool.h -o tool.h.pch" + flags)
#   os.system("clang++ -x c++-header vulkan_config.h -o vulkan_config.h.pch" + flags)
  