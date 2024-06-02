module render.vk.instance;

import render.vk.surface;

namespace rd::vk {

auto Instance::registerExtensions() -> std::vector<const char*> {
  auto extensions = std::vector<const char*>{};

  registerExtension<Surface>(extensions);


  return extensions;
}

} // namespace rd::vk