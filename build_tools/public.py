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
    self.dyndep_dir = ospath.join(self.ninja_dir, 'dyndeps')

    self.target_dir = ospath.join(self.build_dir, 'out')
    self.target_file = ospath.join(self.target_dir, 'hello.exe')

    self.build_tools_dir = ospath.join(self.root_dir, 'build_tools')
    self.dyndep_generate_script = ospath.join(self.build_tools_dir, 'dyndep_generate.py')

    self.clang_executable_path = 'clang'
    self.system_include_dirs = [
      'C:/Users/ZhengyangZhao/msys64/mingw64/include/c++/v1',
      'C:/Users/ZhengyangZhao/msys64/mingw64/lib/clang/17/include',
    ]


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


class Resources:
  def __init__(self):
    self.config_filename = 'resource.yml'
    self.type_sub_dir = 'sub_dir'
    self.type_source = 'source'
    self.type_module_interface = 'module'
    self.type_include_dir = 'include'
    self.type_header_unit = 'header_unit'
    self.type_shader = 'header_unit'

rsc = Resources()

class Ninja:
  def __init__(self):
    self.precompile_header_rule = 'precompile_header'
    self.header_unit_phony = 'header_unit'
    self.dyndep_generator_rule = 'module_dep_scan'
    self.dyndep_module_variable = 'module'
    self.precompile_rule = 'precompile'
    self.compile_rule = 'compile'
    

ninja = Ninja()
