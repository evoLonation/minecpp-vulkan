import json
import os.path as ospath
import os
import subprocess as sp
import ninja_syntax as ninja
import tempfile
import shutil
import yaml
import copy

import public as pub

pub.set_root_dir('./')

def get_all_file():
  # 记录每个resource type的所有资源,资源要么是一个绝对路径，要么是一个(绝对路径，元信息)元组
  resources_dict = {}
  # 记录每个目录的所有子资源的绝对路径
  dir_sub_resources_dict = {}
  # 记录要递归处理的目录的绝对路径
  dir_stack = [pub.path.root_dir]
  while len(dir_stack) != 0:
    current_dir = dir_stack.pop()
    sub_resource_list = []
    with open(ospath.join(current_dir, pub.rsc.config_filename), 'rt') as f:
      config_content = yaml.safe_load(f)
      for resource_type, resources in config_content.items():
        # resources 要不是一个路径组成的数组，要不是一个字典，key为路径，value为元信息
        # 对于sub_dir类型，resources必须是数组
        if resource_type == pub.rsc.type_sub_dir:
          for sub_dir in resources:
            dir_stack.append(ospath.join(current_dir, sub_dir))
        def get_resource_path(resource_name):
          resource_path = ospath.join(current_dir, resource_name)
          sub_resource_list.append(resource_path)
          return resource_path
        if type(resources) == list:
          for resource_name in resources:
            resource_path = get_resource_path(resource_name)
            resources_dict.setdefault(resource_type, []).append(resource_path)
        elif type(resources) == dict:
          for resource_name, resource_info in resources.items():
            resource_path = get_resource_path(resource_name)
            resources_dict.setdefault(resource_type, []).append((resource_path, resource_info))
        dir_sub_resources_dict[current_dir] = sub_resource_list
  return resources_dict, dir_sub_resources_dict

# 为flag添加header pcms
def build_precompile_headers(ninja_writer: ninja.Writer, header_units, flag: pub.Flags):
  ninja_writer.rule(pub.ninja.precompile_header_rule,
                    flag.get_header_precompile('$in', '$out'), 
                    description=f'HEADERUNIT PRECOMPILE $out')

  # add target
  header_pcm_outputs = []
  for header_unit in header_units:
    output = pub.path.get_header_pcm_file(header_unit)
    ninja_writer.build(
      outputs=[output],
      rule=pub.ninja.precompile_header_rule,
      inputs=[header_unit]
    )
    header_pcm_outputs.append(output)
  ninja_writer.build(
    outputs=pub.ninja.header_unit_phony,
    rule='phony',
    inputs=header_pcm_outputs,
  )
  print('header unit build generate done')
  flag.add_header_pcm(header_pcm_outputs)

# 该rule输入一个源文件，会产生对应的dyndep文件，同时产生
def add_module_scan_rule(ninja_writer: ninja.Writer, flag: pub.Flags):
  dyndep_generate_command = sp.list2cmdline([
    'python', pub.path.dyndep_generate_script,
    '-c', '$in',
    '-o', '$out',
    '--root_dir', pub.path.root_dir, 
    '--compile_flag', ' '.join(flag.get_current_flag()),
  ])
  ninja_writer.rule(
    name=pub.ninja.dyndep_generator_rule,
    command=dyndep_generate_command,
    description=f"Module dep scan $out",
  )

def build_module_scan(ninja_writer: ninja.Writer, source):
  ninja_writer.build(
    outputs=pub.path.get_dyndep_file(source),
    rule=pub.ninja.dyndep_generator_rule,
    inputs=source,
    implicit=pub.ninja.header_unit_phony,
  )

# 生成precompile和compile rule
# 这些rule默认$in和$out是编译单元和对应的目标
def add_compile_rule(ninja_writer: ninja.Writer, flag: pub.Flags):
  ninja_writer.rule(pub.ninja.precompile_rule, flag.get_precompile('$in', '$out'), description=f'PRECOMPILE $out')
  ninja_writer.rule(pub.ninja.compile_rule, flag.get_compile('$in', '$out'), description=f'COMPILE $out')

def build_source(ninja_writer: ninja.Writer, source, module):
  dyndep_file = pub.path.get_dyndep_file(source)
  def build(rule, input, output):
    ninja_writer.build(
      outputs=output,
      rule=rule,
      inputs=input,
      implicit=pub.ninja.header_unit_phony,
      order_only=dyndep_file,
      variables={'dyndep': dyndep_file},
    )
  if module:
    build(pub.ninja.precompile_rule, source, pub.path.get_pcm_file(module))
    build(pub.ninja.compile_rule, pub.path.get_pcm_file(module), pub.path.get_obj_file(source))
  else:
    build(pub.ninja.compile_rule, source, pub.path.get_obj_file(source))

def build_link(ninja_writer: ninja.Writer, sources, link_lib_paths, target):
  dir_set = set()
  link_libs = []
  for path in link_lib_paths:
    dir, filename = ospath.split(path)
    dir_set.add(dir)
    if filename[:3] == 'lib' and filename[-2:] == '.a':
      link_libs.append(filename[3:-2])
    elif filename[-4:] == '.lib':
      link_libs.append(filename[:-4])
  ninja_writer.rule(pub.ninja.link_rule, pub.Flags().get_link(['$in'], list(dir_set), link_libs, '$out'))
  ninja_writer.build(target, pub.ninja.link_rule, [pub.path.get_obj_file(source) for source in sources])

def add_sub_ninja(ninja_writer: ninja.Writer, src_ninja, sub_ninja):
  ninja_writer.subninja(ospath.relpath(sub_ninja, ospath.split(src_ninja)[0]))

def execute_ninja(ninja_file, extra = '', stdout = None):
  return sp.run(f'ninja -C {ospath.split(ninja_file)[0]} -f {ospath.split(ninja_file)[1]} {extra}', stdout=stdout)

def get_module_info(source):
  with open(pub.path.get_dyndep_file(source), 'rt') as f:
    for line in f:
      prefix = '# [MODULE INFO] '
      if line.startswith(prefix):
        info = line[len(prefix):].strip()
        return '' if info == '[EMPTY]' else info
  raise RuntimeError(f'the dyndep file {pub.path.get_dyndep_file(source)} not exist module info comment')

if __name__ == '__main__':
  resources_dict, dir_sub_resources_dict = get_all_file()

  flag = pub.Flags()
  flag.add_include_dirs(resources_dict.get(pub.rsc.type_include_dir, []))
  
  # 创建header precompile的ninja文件
  os.makedirs(ospath.split(pub.path.ninja_header_precompile_file)[0], exist_ok=True)
  with open(pub.path.ninja_header_precompile_file, 'wt') as f:
    build_precompile_headers(
      ninja.Writer(f), 
      resources_dict.get(pub.rsc.type_header_unit, []), 
      flag
    )

  sources = resources_dict.get(pub.rsc.type_source, [])

  # 创建module scan的ninja文件
  os.makedirs(ospath.split(pub.path.ninja_module_scan_file)[0], exist_ok=True)
  with open(pub.path.ninja_module_scan_file, 'wt') as f:
    ninja_writer = ninja.Writer(f)
    add_sub_ninja(ninja_writer, pub.path.ninja_module_scan_file, pub.path.ninja_header_precompile_file)
    add_module_scan_rule(ninja_writer, flag)
    for source_path in sources:
      build_module_scan(ninja_writer, source_path)

  # 执行module scan ninja来更新源文件的 provided module info
  print('analysis source module info...')
  execute_ninja(pub.path.ninja_module_scan_file)

  # 创建 compile , precompile 和 link 的 ninja 文件
  os.makedirs(ospath.split(pub.path.ninja_file)[0], exist_ok=True)
  with open(pub.path.ninja_file, 'wt') as f:
    ninja_writer = ninja.Writer(f)
    add_sub_ninja(ninja_writer, pub.path.ninja_file, pub.path.ninja_module_scan_file)
    flag.add_module_pcm_dir(pub.path.pcm_dir)
    add_compile_rule(ninja_writer, flag)
    for source_path in sources:
      build_source(ninja_writer, source_path, get_module_info(source_path))
    
    build_link(ninja_writer, sources, resources_dict.get(pub.rsc.type_link_file, []), 'hello.exe')

  # 创建 compile_commands.json
  with open('compile_commands.json', 'wb') as f:
    result = execute_ninja(pub.path.ninja_file, extra=f'-t compdb {pub.ninja.compile_rule} {pub.ninja.precompile_rule}', stdout=sp.PIPE).stdout
    f.write(result)
  




  
