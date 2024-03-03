import os.path as ospath

class Paths:
  def __init__(self, root_dir_):
    self.root_dir = ospath.abspath(root_dir_)
    self.build_dir = ospath.join(self.root_dir, 'build')

    self.gen_dir = ospath.join(self.build_dir, 'gen')
    self.obj_dir = ospath.join(self.build_dir, 'obj')
    self.pcm_dir = ospath.join(self.build_dir, 'pcm')
    self.header_pcm_dir = ospath.join(self.build_dir, 'hpcm')

    self.ninja_dir = self.build_dir
    self.ninja_file = ospath.join(self.ninja_dir, 'build.ninja')
    self.ninja_header_precompile_file = ospath.join(self.ninja_dir, 'header_precompile.ninja')
    self.ninja_module_scan_file = ospath.join(self.ninja_dir, 'module_scan.ninja')
    self.dyndep_dir = ospath.join(self.ninja_dir, 'dyndeps')

    self.provide_module_info_file = ospath.join(self.build_dir, 'provide_module.json')

    self.target_dir = ospath.join(self.build_dir, 'out')
    self.target_file = ospath.join(self.target_dir, 'hello.exe')

    self.build_tools_dir = ospath.join(self.root_dir, 'build_tools')
    self.dyndep_generate_script = ospath.join(self.build_tools_dir, 'dyndep_generate.py')

  # 得到相对root_dir的路径
  def get_rel_root_path(self, path):
    return ospath.relpath(path, self.root_dir)
  def get_pcm_file(self, module):
    return ospath.abspath(ospath.join(self.pcm_dir, module.replace(':', '-') + '.pcm'))
  def get_obj_file(self, path):
    return ospath.join(self.obj_dir, self.get_rel_root_path(path) + '.o')
  def get_header_pcm_file(self, path):
    return ospath.join(self.header_pcm_dir, ospath.split(path)[1][:-2] + '.pcm')
  def get_dyndep_file(self, path):
    return ospath.join(self.dyndep_dir, self.get_rel_root_path(path) + '.dd')

path = Paths('./')
def set_root_dir(root_dir):
  global path
  path = Paths(root_dir)


class Flags:
  def __init__(self):
    self.clang_executable_path = 'clang'
    self.system_include_dirs = [
      'C:/Users/ZhengyangZhao/msys64/mingw64/include/c++/v1',
      'C:/Users/ZhengyangZhao/msys64/mingw64/lib/clang/17/include',
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
      '-Wno-unused-command-line-argument']
  
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
  
  def get_link(self, objs, link_dirs, link_libs, output):
    return self.current_flag + objs +\
      ['-L'+dir for dir in link_dirs + self.system_link_dirs]+\
      ['-l'+lib for lib in link_libs + self.system_link_libs]+\
      ['-o', output]

flag = Flags()

class Resources:
  def __init__(self):
    self.config_filename = 'resource.yml'
    self.type_sub_dir = 'sub_dir'
    self.type_source = 'source'
    self.type_include_dir = 'include'
    self.type_header_unit = 'header_unit'
    self.type_link_file = 'link'

rsc = Resources()

class Ninja:
  def __init__(self):
    self.precompile_header_rule = 'precompile_header'
    self.header_unit_phony = 'header_unit'
    self.dyndep_generator_rule = 'module_dep_scan'
    self.precompile_rule = 'precompile'
    self.compile_rule = 'compile'
    self.link_rule = 'link'
    

ninja = Ninja()
