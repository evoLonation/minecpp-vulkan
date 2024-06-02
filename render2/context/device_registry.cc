module render.vk.device;

import render.vk.swapchain;
import render.vk.executor;
import render.sampler;
import std;

namespace rd::vk {

auto Device::registerExtensions() -> std::vector<const char*> {
  auto extensions = std::vector<const char*>{};

  registerExtension<Swapchain>(extensions);

  return extensions;
}
auto Device::registerCheckers() -> std::vector<bool (*)(const PdeviceContext&)> {
  auto checkers = std::vector<bool (*)(const PdeviceContext&)>{};

  registerChecker<Swapchain>(checkers);
  registerChecker<CommandExecutor>(checkers);
  registerChecker<SampledTexture>(checkers);

  return checkers;
}

} // namespace rd::vk
