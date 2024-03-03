import json
import os.path as ospath
import os
import subprocess as sp
import ninja_syntax as ninja
import tempfile
import shutil
import yaml

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

def get_flag_with_include(include_dirs):
  base_flag = [
    pub.path.clang_executable_path,
    '-std=c++23', 
    '-fexperimental-library', 
    '-nostdinc++', 
    '-nostdlib++', 
    '-Wno-unused-command-line-argument']
  for system_include in pub.path.system_include_dirs:
    base_flag += ['-isystem', system_include]
  base_flag += list(map(lambda x: '-I'+x, include_dirs))
  return base_flag

# 返回添加了头文件pcm上下文的flag
def build_precompile_headers(ninja_writer: ninja.Writer, header_units, flag):
  # add rule
  header_unit_flag = flag + ['-fmodule-header', '-xc++-header']
  ninja_writer.rule(pub.ninja.precompile_header_rule,
                    header_unit_flag + ['$in', '-o', '$out'], 
                    description=f'PRECOMPILE HEADERUNIT $out')

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
  return flag + \
    list(map(lambda x: f'-fmodule-file={x}', header_pcm_outputs)) + \
    ['-Wno-experimental-header-units']

def add_dyndep_rule(ninja_writer: ninja.Writer, flag):
  dyndep_generate_command = sp.list2cmdline([
    'python', pub.path.dyndep_generate_script, 
    '-c', '$in',
    '-m', f'${pub.ninja.dyndep_module_variable}',
    '-o', '$out',
    '--root_dir', pub.path.root_dir, 
    '--compile_flag', ' '.join(flag),
  ])
  ninja_writer.rule(
    name=pub.ninja.dyndep_generator_rule,
    command=dyndep_generate_command,
    description=f"GENERATE MODULE DYNDEP $out",
  )

# 生成precompile和compile rule
# 这些rule默认$in和$out是编译单元和对应的目标
def add_compile_rule(ninja_writer: ninja.Writer, flag):
  with_pcm_flag = flag + [f'-fprebuilt-module-path={pub.path.pcm_dir}']
  precompile_flag = with_pcm_flag + ['--precompile', '$in', '-o', '$out']
  compile_flag = with_pcm_flag + ['-c', '$in', '-o', '$out']
  ninja_writer.rule(pub.ninja.precompile_rule, precompile_flag, description=f'PRECOMPILE $out')
  ninja_writer.rule(pub.ninja.compile_rule, compile_flag, description=f'COMPILE $out')

def build_source(ninja_writer: ninja.Writer, source, module=None):
  dyndep_file = pub.path.get_dyndep_file(source)
  ninja_writer.build(
    outputs=dyndep_file,
    rule=pub.ninja.dyndep_generator_rule,
    inputs=source,
    implicit=pub.ninja.header_unit_phony,
    variables={pub.ninja.dyndep_module_variable: module if module != None else ''},
  )
  ninja_writer.build(
    outputs=pub.path.get_obj_file(source),
    rule=pub.ninja.compile_rule,
    inputs=source,
    implicit=pub.ninja.header_unit_phony,
    order_only=dyndep_file,
    variables={'dyndep': dyndep_file},
  )
  if module != None:
    ninja_writer.build(
      outputs=pub.path.get_pcm_file(module),
      rule=pub.ninja.precompile_rule,
      inputs=source,
      implicit=pub.ninja.header_unit_phony,
      order_only=dyndep_file,
      variables={'dyndep': dyndep_file},
    )
    

if __name__ == '__main__':
  resources_dict, dir_sub_resources_dict = get_all_file()

  flag_with_include = get_flag_with_include(resources_dict.get(pub.rsc.type_include_dir, []))
  os.makedirs(ospath.split(pub.path.ninja_file)[0], exist_ok=True)
  with open(pub.path.ninja_file, 'wt') as f:
    ninja_writer = ninja.Writer(f)
    flag_with_header_pcms = build_precompile_headers(
      ninja_writer, 
      resources_dict.get(pub.rsc.type_header_unit, []), 
      flag_with_include
    )
    print(flag_with_header_pcms)
    add_dyndep_rule(ninja_writer, flag_with_header_pcms)
    add_compile_rule(ninja_writer, flag_with_header_pcms)
    for source_path in resources_dict.get(pub.rsc.type_source, []):
      build_source(ninja_writer, source_path)
    for source_path, module in resources_dict.get(pub.rsc.type_module_interface, []):
      build_source(ninja_writer, source_path, module)