module render.vk.device;

import "vulkan_config.h";
import render.vk.tool;
import toy;

namespace rd::vk {

Device::Device(std::span<DeviceCapabilityChecker> checkers) {
  auto devices = getVkResources(vkEnumeratePhysicalDevices, rs::Instance::getInstance()) |
                 views::transform([](auto handle) { return PhysicalDevice{ handle }; }) |
                 ranges::to<std::vector>();

  auto supported_devices =
    std::vector<std::pair<PhysicalDevice, std::vector<DeviceCapabilityBuilder>>>{};
  for (auto& device : devices) {
    auto requests = std::vector<DeviceCapabilityBuilder>{};
    auto is_support = [&]() {
      for (auto& checker : checkers) {
        auto request = DeviceCapabilityBuilder{ device };
        auto res = checker(request);
        if (res) {
          requests.push_back(request);
        } else {
          return false;
        }
      }
      return true;
    }();
    if (is_support) {
      supported_devices.emplace_back(device, requests);
    }
  }
  if (supported_devices.empty()) {
    toy::throwf("no support physical device");
  }
  toy::debugf("support devices: {::}", supported_devices | views::transform([](auto& pair) {
                                         return pair.first.getProperties().deviceName;
                                       }));

  auto selected_device = std::pair<PhysicalDevice, std::vector<DeviceCapabilityBuilder>>{};
  if (auto res = ranges::find_if(
        supported_devices,
        [](auto& pair) {
          return pair.first.getProperties().deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        }
      );
      res != supported_devices.end()) {
    selected_device = *res;
  }
  selected_device = supported_devices[0];

  toy::debugf("select device {}", selected_device.first.getProperties().deviceName);

  auto& requests = selected_device.second;

  auto queue_family_counts = std::map<uint32, uint32>{};
  for (auto& request : requests) {
    for (auto [index, count] : request.family_queue_counts) {
      queue_family_counts[index] += count;
    }
  }

  auto priorities = std::vector<float>{};
  auto queue_create_infos = std::vector<VkDeviceQueueCreateInfo>{};
  if (!queue_family_counts.empty()) {
    priorities.resize(*ranges::max_element(queue_family_counts | views::values));
    queue_create_infos.append_range( //
      queue_family_counts | views::transform([&](auto& pair) {
        return VkDeviceQueueCreateInfo{
          .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
          .queueFamilyIndex = pair.first,
          .queueCount = pair.second,
          .pQueuePriorities = priorities.data(),
        };
      })
    );
  }
  auto required_extensions = requests |
                             views::transform([](auto& request) { return request.extensions; }) |
                             views::join | ranges::to<std::vector>();
  auto unique_required_extensions =
    ranges::subrange(required_extensions.begin(), ranges::unique(required_extensions).begin()) |
    views::transform([](auto& str) { return str.data(); }) | ranges::to<std::vector>();

  auto enabled_features = VkPhysicalDeviceFeatures{};
  for (auto& request : requests) {
    for (auto member : request.features) {
      enabled_features.*member = true;
    }
  }
  auto enabled_vk13features = VkPhysicalDeviceVulkan13Features{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
  };
  for (auto& request : requests) {
    for (auto member : request.vk13features) {
      enabled_vk13features.*member = true;
    }
  }
  auto enabled_vk12features = VkPhysicalDeviceVulkan12Features{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    .pNext = &enabled_vk13features,
  };
  for (auto& request : requests) {
    for (auto member : request.vk12features) {
      enabled_vk12features.*member = true;
    }
  }

  auto create_info = VkDeviceCreateInfo{
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = &enabled_vk12features,
    .queueCreateInfoCount = static_cast<uint32>(queue_create_infos.size()),
    .pQueueCreateInfos = queue_create_infos.data(),
    .enabledExtensionCount = static_cast<uint32>(unique_required_extensions.size()),
    .ppEnabledExtensionNames = unique_required_extensions.data(),
    .pEnabledFeatures = &enabled_features,
    // .pEnabledFeatures = &selected_device.first.getFeatures(),
  };
  toy::debugf("enable extensions: {::}", unique_required_extensions);

  // 旧实现在 (instance, physical device) 和 (logic device以上) 两个层面有不同的
  // layer, 而在新实现中合并了 不再需要定义 enabledLayerCount 和
  // ppEnabledLayerNames createInfo.enabledLayerCount =
  // static_cast<uint32>(requiredLayers_.size());
  // createInfo.ppEnabledLayerNames = requiredLayers_.data();
  rs::Device::setInvalid();
  rs::Device::operator=({ selected_device.first.get(), create_info });
  _features = enabled_features;
  _pdevice = selected_device.first;
}

PhysicalDevice::PhysicalDevice(VkPhysicalDevice pdevice) {
  _handle = pdevice;
  vkGetPhysicalDeviceProperties(pdevice, &_properties);
  auto features2 =
    VkPhysicalDeviceFeatures2{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
  features2.pNext = &_vk12features;
  _vk12features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
  _vk12features.pNext = &_vk13features;
  _vk13features = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
  vkGetPhysicalDeviceFeatures2(pdevice, &features2);
  _features = features2.features;
  vkGetPhysicalDeviceMemoryProperties(pdevice, &_memory_properties);
  _queue_family_properties = getVkResources(vkGetPhysicalDeviceQueueFamilyProperties, pdevice);

  auto extension_properties =
    getVkResources(vkEnumerateDeviceExtensionProperties, pdevice, nullptr);
  toy::debugf(
    "the supported {} extensions are :\n {::}",
    extension_properties.size(),
    extension_properties |
      views::transform([](auto properties) { return std::string{ properties.extensionName }; })
  );
  _extension_properties.insert_range(extension_properties | views::transform([](auto properties) {
                                       return std::pair{ std::string{ properties.extensionName },
                                                         properties };
                                     }));
}

auto PhysicalDevice::checkFormatSupport(
  FormatTarget type, VkFormatFeatureFlags features, std::span<VkFormat const> formats
) const -> bool {
  return ranges::all_of(formats, [&](auto format) {
    VkFormatProperties properties;
    if (_format_properties.contains(format)) {
      properties = _format_properties[format];
    } else {
      vkGetPhysicalDeviceFormatProperties(_handle, format, &properties);
      _format_properties[format] = properties;
    }
    auto support_features = (VkFormatFeatureFlags)0;
    switch (type) {
    case FormatTarget::BUFFER:
      support_features = properties.bufferFeatures;
      break;
    case FormatTarget::OPTIMAL_TILING:
      support_features = properties.optimalTilingFeatures;
      break;
    case FormatTarget::LINEAR_TILING:
      support_features = properties.linearTilingFeatures;
      break;
    }
    return (support_features & features) == features;
  });
  return true;
}

} // namespace rd::vk
