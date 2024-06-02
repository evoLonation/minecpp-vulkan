module render.vk.device;

import "vulkan_config.h";
import render.vk.tool;
import render.vk.instance;
import render.vk.executor;
import toy;

namespace rd::vk {

auto pickPhysicalDevice(
  std::span<const char* const>               required_extensions,
  std::span<bool (*)(const PdeviceContext&)> checkers
) -> PdeviceContext {
  std::vector<VkPhysicalDevice> devices =
    getVkResources(vkEnumeratePhysicalDevices, Instance::getInstance().instance());
  std::vector<PdeviceContext> supported_devices;
  for (auto device : devices) {
    auto ctx = PdeviceContext{ device };
    vkGetPhysicalDeviceProperties(device, &ctx.properties);
    vkGetPhysicalDeviceFeatures(device, &ctx.features);

    toy::debugf("checking physical device {}:", ctx.properties.deviceName);
    bool unsupport = false;

    try {
      checkAvaliableSupports(
        required_extensions,
        getVkResources(vkEnumerateDeviceExtensionProperties, device, nullptr),
        [](auto& extension) { return extension.extensionName; }
      );
    } catch (const std::exception& e) {
      toy::debug(e.what());
      unsupport = true;
    }

    for (auto& checker : checkers) {
      if (!checker(ctx)) {
        unsupport = true;
      }
    }

    if (!unsupport) {
      supported_devices.push_back(ctx);
    }
  }
  if (supported_devices.empty()) {
    toy::throwf("no support physical device");
  }
  toy::debugf("support devices: {::}", supported_devices | views::transform([](auto& info) {
                                         return info.properties.deviceName;
                                       }));
  auto selected_device = supported_devices[0];
  toy::debugf("select device {}", selected_device.properties.deviceName);
  return selected_device;
}

// todo: change queue_create_info to anyview
auto createDevice(
  VkPhysicalDevice                         pdevice,
  std::span<const VkDeviceQueueCreateInfo> queue_create_infos,
  VkPhysicalDeviceFeatures                 enabled_features,
  std::span<const char* const>             required_extensions
) -> rs::Device {

  VkDeviceCreateInfo create_info{
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
    .pQueueCreateInfos = queue_create_infos.data(),
    .enabledExtensionCount = static_cast<uint32_t>(required_extensions.size()),
    .ppEnabledExtensionNames = required_extensions.data(),
    .pEnabledFeatures = &enabled_features,
  };

  // 旧实现在 (instance, physical device) 和 (logic device以上) 两个层面有不同的
  // layer, 而在新实现中合并了 不再需要定义 enabledLayerCount 和
  // ppEnabledLayerNames createInfo.enabledLayerCount =
  // static_cast<uint32_t>(requiredLayers_.size());
  // createInfo.ppEnabledLayerNames = requiredLayers_.data();

  return rs::Device{ pdevice, create_info };
}

Device::Device() {
  auto checkers = registerCheckers();
  auto extensions = registerExtensions();
  auto ctx = pickPhysicalDevice(extensions, checkers);
  _pdevice = ctx.device;
  _properties = ctx.properties;
  _features = ctx.features;

  rs::Device::operator=({ createDevice(
    _pdevice, CommandExecutor::getQueueInfo().queue_create_infos, _features, extensions
  ) });
}

} // namespace rd::vk
