import os.path as ospath
from enum import Enum
import subprocess as sp

class TargetType(Enum):
  EXECUTABLE = 0
  DYNAMIC_LIBRARY = 1

class Paths:
  def __init__(self, root_dir_: str, target_: str, type_: TargetType):
    self.target_type = type_
    self.root_dir = ospath.abspath(root_dir_)
    self.target = target_
    self.build_dir = ospath.join(self.root_dir, 'build')

    self.gen_dir = ospath.join(self.build_dir, 'gen')
    self.obj_dir = ospath.join(self.build_dir, 'obj')
    self.pcm_dir = ospath.join(self.build_dir, 'pcm')
    self.header_pcm_dir = ospath.join(self.build_dir, 'hpcm')

    self.ninja_dir = self.build_dir
    self.ninja_file = ospath.join(self.ninja_dir, 'build.ninja')
    self.ninja_header_precompile_file = ospath.join(self.ninja_dir, 'header_precompile.ninja')
    self.ninja_module_scan_file = ospath.join(self.ninja_dir, 'module_scan.ninja')
    self.ninja_shader_code_gen_file = ospath.join(self.ninja_dir, 'shader_gen.ninja')
    self.dyndep_dir = ospath.join(self.ninja_dir, 'dyndeps')

    self.provide_module_info_file = ospath.join(self.build_dir, 'provide_module.json')

    self.target_dir = ospath.join(self.build_dir, 'out')
    if self.target_type == TargetType.EXECUTABLE:
      self.target_file = ospath.join(self.target_dir, self.target+'.exe')
    elif self.target_type == TargetType.DYNAMIC_LIBRARY:
      self.target_file = ospath.join(self.target_dir, self.target+'.dll')

    self.build_tools_dir = ospath.dirname(ospath.abspath(__file__))
    self.dyndep_generate_script = ospath.join(self.build_tools_dir, 'dyndep_generate.py')
    self.shader_generate_script = ospath.join(self.build_tools_dir, 'shader_code_generate.py')

    self.gen_dir = ospath.join(self.build_dir, 'gen')
    self.shader_code_all_file = ospath.join(self.gen_dir, 'shader_code.cc')

  # 得到相对root_dir的路径
  def get_rel_root_path(self, path):
    return ospath.relpath(path, self.root_dir)
  def get_pcm_file(self, module):
    return ospath.abspath(ospath.join(self.pcm_dir, module.replace(':', '-') + '.pcm'))
  def get_obj_file(self, path):
    return ospath.join(self.obj_dir, self.get_rel_root_path(path) + '.o')
  def get_header_pcm_file(self, path):
    return ospath.join(self.header_pcm_dir, ospath.basename(path)[:-2] + '.pcm')
  def get_dyndep_file(self, path):
    return ospath.join(self.dyndep_dir, self.get_rel_root_path(path) + '.dd')
  def get_shader_code_file(self, shader_file):
    return ospath.join(self.gen_dir, self.get_rel_root_path(shader_file)+'.ccm')
  def get_dylib_target_file(self, dylib_file):
    return ospath.join(self.target_dir, ospath.basename(dylib_file))


path = Paths('./', 'test', TargetType.EXECUTABLE)
def set_path(root_dir: str, target: str, type: TargetType = TargetType.EXECUTABLE):
  global path
  path = Paths(root_dir, target, type)


class Flags:
  def __init__(self):
    self.clang_executable_path = 'clang'
    self.system_include_dirs = [
      'C:/Users/ZhengyangZhao/msys64/mingw64/include/c++/v1',
      'C:/Users/ZhengyangZhao/msys64/mingw64/lib/clang/18/include',
    ]
    self.system_link_dirs = [
      'C:/Users/18389/msys2/mingw64/lib',
    ]
    self.system_link_libs = [
      'c++',
    ]
    self.header_unit_fix_flag = ['-fretain-comments-from-system-headers']

    # if current_flag != None:
    #   self.current_flag = current_flag
    # else:
    self.current_flag = [
      self.clang_executable_path,
      '-std=c++23', 
      '-fexperimental-library', 
      '-nostdinc++', 
      '-nostdlib++', 
      '-Wno-unused-command-line-argument',
      # for a deprecation bug occured in clang18 with std module: 
      # https://github.com/llvm/llvm-project/issues/75057
      '-Wno-deprecated-declarations', 
      '-g']
  
  def get_current_flag(self):
    return self.current_flag
  def add_include_dirs(self, include_dirs):
    for system_include in self.system_include_dirs:
      self.current_flag += ['-isystem', system_include]
    self.current_flag += list(map(lambda x: '-I'+x, include_dirs))
  
  def get_header_precompile(self, input, output):
    return self.current_flag + self.header_unit_fix_flag+\
      ['-fmodule-header', '-xc++-header', input, '-o', output]
  
  def add_header_pcm(self, header_pcms):
    self.current_flag += ['-Wno-experimental-header-units'] + self.header_unit_fix_flag +\
      list(map(lambda x: f'-fmodule-file={x}', header_pcms))
  
  def add_module_pcm_dir(self, module_pcm_dir):
    self.current_flag += [f'-fprebuilt-module-path={module_pcm_dir}']
  
  def get_precompile(self, input, output):
    return self.current_flag + ['--precompile', input, '-o', output]
  
  def get_compile(self, input, output):
    return self.current_flag + ['-c', input, '-o', output]
  
  def get_link(self, objs, link_dirs, link_libs, output, target_type: TargetType):
    return self.current_flag + objs +\
      (['-shared'] if target_type == TargetType.DYNAMIC_LIBRARY else [])+\
      ['-L'+dir for dir in link_dirs + self.system_link_dirs]+\
      ['-l'+lib for lib in link_libs + self.system_link_libs]+\
      ['-o', output]

flag = Flags()

class Resources:
  def __init__(self):
    self.config_filename = 'resource.yml'
    self.type_sub_dir = 'sub_dir'
    self.type_source = 'source'
    self.type_include_dir = 'include_dir'
    self.type_header_unit = 'header_unit'
    self.type_lib_file = 'lib'
    self.type_dylib_file = 'dylib'
    self.type_shader = 'shader'

rsc = Resources()

class Ninja:
  def __init__(self):
    self.precompile_header_rule = 'precompile_header'
    self.header_unit_phony = 'header_unit'
    self.dyndep_generator_rule = 'module_dep_scan'
    self.precompile_rule = 'precompile'
    self.compile_rule = 'compile'
    self.link_rule = 'link'
    self.copy_rule = 'copy'
    self.shader_code_rule = 'shader_code_generate'
    self.shader_code_total_rule = 'shader_code_total_generate'

  def execute(self, ninja_file, extra = '', stdout = None):
    return sp.run(f'ninja -C {ospath.dirname(ninja_file)} -f {ospath.basename(ninja_file)} {extra}', stdout=stdout)
  
  def module_phony(self, module_name):
    return ospath.join('mod', module_name)

ninja = Ninja()
