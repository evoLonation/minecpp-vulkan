import os.path as ospath
import os
import subprocess as sp
import ninja_syntax as ninja
import yaml
import public as pub
import argparse

def init_arg_parser():
  parser = argparse.ArgumentParser()
  parser.add_argument('--root', type=str, dest='root', required=False, default='./')
  subparsers = parser.add_subparsers(dest = 'task')

  precompile_task = subparsers.add_parser('precompile', help='precompile a module interface file')
  precompile_task.add_argument('file', type=str, help='the file need to be precompiled')

  precompile_task = subparsers.add_parser('build', help='build whole project')

  return parser.parse_args()

def get_all_file():
  # 记录每个resource type的所有资源,资源要么是一个绝对路径，要么是一个(绝对路径，元信息)元组
  resources_dict = {}

  # 记录要递归处理的目录的绝对路径
  dir_stack = [pub.path.root_dir]
  while len(dir_stack) != 0:
    current_dir = dir_stack.pop()
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
          return resource_path
        if type(resources) == list:
          for resource_name in resources:
            resource_path = get_resource_path(resource_name)
            resources_dict.setdefault(resource_type, []).append(resource_path)
        elif type(resources) == dict:
          for resource_name, resource_info in resources.items():
            resource_path = get_resource_path(resource_name)
            resources_dict.setdefault(resource_type, []).append((resource_path, resource_info))
  return resources_dict

# 为flag添加header pcms
def build_precompile_headers(writer: ninja.Writer, header_units, flag: pub.Flags):
  writer.rule(pub.ninja.precompile_header_rule,
                    flag.get_header_precompile('$in', '$out'), 
                    description=f'HEADERUNIT PRECOMPILE $out')

  # add target
  header_pcm_outputs = []
  for header_unit in header_units:
    output = pub.path.get_header_pcm_file(header_unit)
    writer.build(
      outputs=[output],
      rule=pub.ninja.precompile_header_rule,
      inputs=[header_unit]
    )
    header_pcm_outputs.append(output)
  writer.build(
    outputs=pub.ninja.header_unit_phony,
    rule='phony',
    inputs=header_pcm_outputs,
  )
  flag.add_header_pcm(header_pcm_outputs)

def build_module_scan(writer: ninja.Writer, sources, flag: pub.Flags):
  dyndep_generate_command = sp.list2cmdline([
    'python', pub.path.dyndep_generate_script,
    '-c', '$in',
    '-o', '$out',
    '--root_dir', pub.path.root_dir, 
    '--compile_flag', ' '.join(flag.get_current_flag()),
  ])
  writer.rule(
    name=pub.ninja.dyndep_generator_rule,
    command=dyndep_generate_command,
    description=f"Module dep scan $out",
  )
  for source in sources:
    writer.build(
      outputs=pub.path.get_dyndep_file(source),
      rule=pub.ninja.dyndep_generator_rule,
      inputs=source,
      implicit=pub.ninja.header_unit_phony,
    )

def build_source(writer: ninja.Writer, sources, flag: pub.Flags):
  writer.rule(pub.ninja.precompile_rule, flag.get_precompile('$in', '$out'), description=f'PRECOMPILE $out')
  writer.rule(pub.ninja.compile_rule, flag.get_compile('$in', '$out'), description=f'COMPILE $out')
  for source, module in sources:
    dyndep_file = pub.path.get_dyndep_file(source)
    def build(rule, input, output):
      writer.build(
        outputs=output,
        rule=rule,
        inputs=input,
        implicit=pub.ninja.header_unit_phony,
        order_only=dyndep_file,
        variables={'dyndep': dyndep_file},
      )
    if module is False:
      build(pub.ninja.compile_rule, source, pub.path.get_obj_file(source))
    else:
      build(pub.ninja.precompile_rule, source, pub.path.get_pcm_file(module))
      build(pub.ninja.compile_rule, pub.path.get_pcm_file(module), pub.path.get_obj_file(source))
      writer.build(outputs=pub.ninja.module_phony(module), rule='phony', inputs=pub.path.get_pcm_file(module))
  dir_sources_dict = {}
  for source, _ in sources:
    sub_dir = ospath.dirname(source)
    while True:
      dir_sources_dict.setdefault(pub.path.get_rel_root_path(sub_dir), []).append(source)
      if pub.path.root_dir == sub_dir:
        break
      sub_dir = ospath.dirname(sub_dir)
  for dir, sources in dir_sources_dict.items():
    writer.build(outputs=ospath.join('dir', dir), rule='phony', 
                 inputs=[pub.path.get_obj_file(source) for source in sources])

def build_link(writer: ninja.Writer, sources, link_lib_paths):
  dir_set = set()
  link_libs = []
  for path in link_lib_paths:
    dir, filename = ospath.split(path)
    dir_set.add(dir)
    str().endswith
    if filename.startswith('lib') and filename.endswith('.a'):
      link_libs.append(filename[3:-2])
    elif filename.endswith('.lib') or filename.endswith('.dll'):
      link_libs.append(filename[:-4])
  writer.rule(pub.ninja.link_rule, pub.Flags().get_link(['$in'], list(dir_set), link_libs, '$out', pub.path.target_type), "LINK $out")
  writer.build(pub.path.target_file, pub.ninja.link_rule, [pub.path.get_obj_file(source) for source, _ in sources])
    
def add_sub_ninja(ninja_writer: ninja.Writer, src_ninja, sub_ninja):
  ninja_writer.subninja(ospath.relpath(sub_ninja, ospath.dirname(src_ninja)))

def get_module_info(source):
  with open(pub.path.get_dyndep_file(source), 'rt') as f:
    for line in f:
      prefix = '# [MODULE INFO] '
      if line.startswith(prefix):
        info = line[len(prefix):].strip()
        return False if info == '[EMPTY]' else info
  raise RuntimeError(f'the dyndep file {pub.path.get_dyndep_file(source)} not exist module info comment')

def build_shader_code_generate(ninja_writer: ninja.Writer, shader_files):
  # 生成 single shader_code
  ninja_writer.rule(
    name=pub.ninja.shader_code_rule,
    command=['python', pub.path.shader_generate_script,
             '--task', 'single',
             '-c', '$in',
             '-o', '$out'],
    description='SHADERCODE single generate $out'
  )
  sources = []
  for file in shader_files:
    ninja_writer.build(
      rule=pub.ninja.shader_code_rule,
      outputs=pub.path.get_shader_code_file(file),
      inputs=file,
    )
    result = sp.run(' '.join(['python', pub.path.shader_generate_script, '--task', 'module', '-c', file]), stdout=sp.PIPE).stdout
    sources.append((pub.path.get_shader_code_file(file), result.decode('utf-8').strip()))

  # 生成 total shader_code
  ninja_writer.rule(
    name=pub.ninja.shader_code_total_rule,
    command=['python', pub.path.shader_generate_script,
             '--task', 'total',
             '-c'] + shader_files +\
             ['-o', '$out'],
    description='SHADERCODE total generate $out'
  )
  ninja_writer.build(
    rule=pub.ninja.shader_code_total_rule,
    outputs=pub.path.shader_code_all_file,
  )
  return sources + [(pub.path.shader_code_all_file, False)]

class NinjaWriterContextManager(ninja.Writer):
  def __enter__(self):
    return self
  def __exit__(self, exc_type, exc_value, traceback):
    self.close()

def open_ninja(path):
  os.makedirs(ospath.dirname(path), exist_ok=True)
  return NinjaWriterContextManager(open(path, 'wt'))

def build_ninja():
  # 遍历项目的所有配置文件，得到所有类型的资源
  resources_dict = get_all_file()

  def get_resource(resource_type):
    return resources_dict.get(resource_type, [])

  flag = pub.Flags()
  flag.add_include_dirs(get_resource(pub.rsc.type_include_dir))
  

  sources = get_resource(pub.rsc.type_source)
  # 尚未生成的sources，是一个(source, module)的列表
  need_gen_sources = []

  # 添加shader generate的ninja文件
  print('build shader generate ninja file...')
  with open_ninja(pub.path.ninja_shader_code_gen_file) as writer:
    shader_resources = get_resource(pub.rsc.type_shader)
    if len(shader_resources) != 0:
      need_gen_sources += build_shader_code_generate(writer, shader_resources)
  
  # 创建header precompile的ninja文件
  print('build header precompile ninja file...')
  with open_ninja(pub.path.ninja_header_precompile_file) as writer:
    build_precompile_headers(writer, get_resource(pub.rsc.type_header_unit), flag)

  # 创建module scan的ninja文件
  print('build module scan ninja file...')
  with open_ninja(pub.path.ninja_module_scan_file) as writer:
    add_sub_ninja(writer, pub.path.ninja_module_scan_file, pub.path.ninja_header_precompile_file)
    build_module_scan(writer, sources + [info[0] for info in need_gen_sources], flag)
  # 执行module scan ninja来更新源文件的 provided module info
  print('analysis source module info...')
  pub.ninja.execute(pub.path.ninja_module_scan_file, extra=' '.join(pub.path.get_dyndep_file(source) for source in sources))

  # 根据刚刚更新的 sources 的 dyndep 文件，为source扩展模块信息
  sources = list(map(lambda source: (source, get_module_info(source)), sources)) + need_gen_sources

  # 创建 compile , precompile 和 link 的 ninja 文件
  with open_ninja(pub.path.ninja_file) as writer:
    add_sub_ninja(writer, pub.path.ninja_file, pub.path.ninja_module_scan_file)
    add_sub_ninja(writer, pub.path.ninja_file, pub.path.ninja_shader_code_gen_file)
    flag.add_module_pcm_dir(pub.path.pcm_dir)
    build_source(writer, sources, flag)
    build_link(writer, sources, get_resource(pub.rsc.type_lib_file))
    writer.rule(pub.ninja.copy_rule, 'cmd.exe /c copy /Y $in $out  > NUL', 'COPY dynamic library $out')
    for dylib in get_resource(pub.rsc.type_dylib_file):
      writer.build(outputs=pub.path.get_dylib_target_file(dylib), rule=pub.ninja.copy_rule, inputs=dylib)
    writer.build(outputs=pub.path.target,
                 rule='phony',
                 inputs=[pub.path.target_file] +
                 [pub.path.get_dylib_target_file(dylib) for dylib in get_resource(pub.rsc.type_dylib_file)])
  # 创建 compile_commands.json
  with open('compile_commands.json', 'wb') as f:
    result = pub.ninja.execute(pub.path.ninja_file, extra=f'-t compdb {pub.ninja.compile_rule} {pub.ninja.precompile_rule}', stdout=sp.PIPE).stdout
    f.write(result)

def precompile_by_module(module_name: str):
  if module_name is False:
    raise RuntimeError("the file need to be precompiled is not a module interface file")
  print(f'precompile the [{module_name}] module')
  pub.ninja.execute(pub.path.ninja_file, pub.ninja.module_phony(module_name))

def precompile_by_file(file_path: str):
  module_name = get_module_info(file_path)
  if not module_name:
    raise RuntimeError(f"The file {file_path} is not a module interface file")
  precompile_by_module(module_name)


if __name__ == '__main__':
  args = init_arg_parser()
  pub.set_path(args.root, 'hello', pub.TargetType.EXECUTABLE)
  if args.task == 'precompile':
    build_ninja()
    precompile_by_file(args.file)
  elif args.task == 'build':
    build_ninja()
    pub.ninja.execute(pub.path.ninja_file)
  else:
    build_ninja()
