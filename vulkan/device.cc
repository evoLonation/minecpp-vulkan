module vulkan.device;

import "vulkan_config.h";
import vulkan.tool;
import toy;

namespace vk {

auto pickPhysicalDevice(
  VkInstance                          instance,
  VkSurfaceKHR                        surface,
  std::span<const char*>              required_extensions,
  DeviceChecker                       device_checker,
  SurfaceChecker                      surface_checker,
  std::span<const QueueFamilyChecker> queue_chekers,
  bool                                all_queue_diff
) -> PhysicalDeviceInfo {
  std::vector<VkPhysicalDevice>   devices = getVkResources(vkEnumeratePhysicalDevices, instance);
  std::vector<PhysicalDeviceInfo> supported_devices;
  for (auto device : devices) {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device, &device_properties);
    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(device, &device_features);
    auto presentModes = getVkResources(vkGetPhysicalDeviceSurfacePresentModesKHR, device, surface);
    auto formats = getVkResources(vkGetPhysicalDeviceSurfaceFormatsKHR, device, surface);

    toy::debugf("checking physical device {}:", device_properties.deviceName);
    bool unsupport = false;
    if (!device_checker(DeviceCheckContext{ device_properties, device_features })) {
      unsupport = true;
    }
    auto selected_surface =
      surface_checker(SurfaceCheckContext{ std::move(presentModes), std::move(formats) });
    if (!selected_surface.has_value()) {
      unsupport = true;
    }

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

    auto queue_family_indices =
      getQueueFamilyIndices(device, surface, queue_chekers, all_queue_diff);
    if (!queue_family_indices.has_value()) {
      unsupport = true;
    }

    if (!unsupport) {
      supported_devices.push_back({ .device = device,
                                    .properties = device_properties,
                                    .features = device_features,
                                    .present_mode = selected_surface->present_mode,
                                    .surface_format = selected_surface->surface_format,
                                    .queue_indices = queue_family_indices.value() });
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

/**
 * @brief 二分图匹配算法
 */
auto hungarian(std::span<const std::span<const int>> graph, int right_count)
  -> std::optional<std::vector<int>> {

  auto left_count = (int)graph.size();
  auto match = std::vector<int>(right_count);
  ranges::fill(match, -1);
  auto found = std::vector<bool>(right_count);

  auto results = std::vector<int>(left_count);

  std::function<bool(int)> dfs = [&](int u) -> bool {
    toy::debugf("u: {}", u);
    for (auto v : graph[u]) {
      toy::debugf("u {} lookup {}", u, v);
      if (!found[v]) {
        found[v] = true;
        if (match[v] == -1 || dfs(match[v])) {
          toy::debugf("u {} select {}", u, v);
          match[v] = u;
          results[u] = v;
          return true;
        }
      }
    }
    toy::debugf("u {} no satisfied select", u);
    return false;
  };

  if (!ranges::all_of(views::iota(0u, graph.size()), [&dfs, &found](int u) {
        ranges::fill(found, false);
        return dfs(u);
      })) {
    return std::nullopt;
  } else {
    return results;
  }
}

auto getQueueFamilyIndices(
  VkPhysicalDevice                    device,
  VkSurfaceKHR                        surface,
  std::span<const QueueFamilyChecker> queue_chekers,
  bool                                all_queue_diff
) -> std::optional<QueueIndices> {

  auto queue_families = getVkResources(vkGetPhysicalDeviceQueueFamilyProperties, device);

  auto family_size = queue_families.size();
  auto request_size = queue_chekers.size();
  toy::debugf("queue family size: {}, queue request size: {}", family_size, request_size);

  std::vector<std::vector<int>> graph(request_size);

  auto family_offsets = std::vector<int>(queue_families.size());
  int  now_offset = 0;
  for (auto [properties, offset] : views::zip(queue_families, family_offsets)) {
    offset = now_offset;
    now_offset += properties.queueCount;
  }

  for (auto [family_i, properties] : queue_families | toy::enumerate) {
    auto queue_count = (int)properties.queueCount;
    toy::debugf("check queue family {}, which has {} queues", family_i, queue_count);
    for (auto&& [request_i, queue_checker] : queue_chekers | toy::enumerate) {
      if (queue_checker(QueueFamilyCheckContext{ device, surface, family_i, properties })) {
        if (!all_queue_diff) {
          graph[request_i].append_range(
            views::iota(family_offsets[family_i], family_offsets[family_i] + queue_count)
          );
        } else {
          graph[request_i].push_back(family_i);
        }
        toy::debugf("queue request {} success", request_i);
      } else {
        toy::debugf("queue request {} failed", request_i);
      }
    }
  }
  auto graph_span = graph |
                    views::transform([](const auto& vector) { return std::span{ vector }; }) |
                    ranges::to<std::vector>();

  toy::debugf("family offsets: {}", family_offsets);
  auto mapper = [&](int v) {
    // reverse iterator.base() pointer to next element
    uint32_t family_i =
      ranges::lower_bound(views::reverse(family_offsets), v, std::greater{}).base() -
      family_offsets.begin() - 1;
    uint32_t queue_i = v - family_offsets[family_i];
    if (all_queue_diff) {
      family_i = v;
      queue_i = 0;
    }
    toy::debugf("edge {} map to {}", v, std::pair{ family_i, queue_i });
    return std::pair{ family_i, queue_i };
  };
  return hungarian(graph_span, now_offset).transform([&](auto results) {
    return results | views::transform(mapper) | ranges::to<std::vector>();
  });
}

auto checkPhysicalDeviceSupport(const DeviceCheckContext& ctx) -> bool {
  return toy::checkDebugf(
           ctx.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
           "device not satisfied VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU"
         ) &&
         toy::checkDebugf(
           ctx.features.geometryShader == VK_TRUE, "device not support geometryShader feature"
         ) &&
         toy::checkDebugf(
           ctx.features.samplerAnisotropy == VK_TRUE, "device not support samplerAnisotropy feature"
         );
}

auto checkSurfaceSupport(const SurfaceCheckContext& ctx) -> std::optional<SelectedSurfaceInfo> {
  auto p_format = ranges::find_if(ctx.surface_formats, [](auto format) {
    return format.format == VK_FORMAT_B8G8R8A8_SRGB &&
           format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  });
  if (p_format == ctx.surface_formats.end()) {
    toy::debugf("no suitable format");
    return std::nullopt;
  }
  /*
   * VK_PRESENT_MODE_IMMEDIATE_KHR: 图像提交后直接渲染到屏幕上
   * VK_PRESENT_MODE_FIFO_KHR:
   * 有一个队列，队列以刷新率的速度消耗图像显示在屏幕上，图像提交后入队，队列满时等待（也即只能在
   * "vertical blank" 时刻提交图像） VK_PRESENT_MODE_FIFO_RELAXED_KHR:
   * 当图像提交时，若队列为空，就直接渲染到屏幕上，否则同上
   * VK_PRESENT_MODE_MAILBOX_KHR:
   * 当队列满时，不阻塞而是直接将队中图像替换为已提交的图像
   */
  auto p_present_mode = ranges::find(ctx.present_modes, VK_PRESENT_MODE_FIFO_KHR);
  if (p_present_mode == ctx.present_modes.end()) {
    toy::debugf("no suitable present mode");
    return std::nullopt;
  }
  return { { *p_present_mode, *p_format } };
}

auto checkGraphicQueue(const QueueFamilyCheckContext& ctx) -> bool {
  return ctx.properties.queueFlags & VK_QUEUE_GRAPHICS_BIT;
}
auto checkPresentQueue(const QueueFamilyCheckContext& ctx) -> bool {
  VkBool32 presentSupport = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(ctx.device, ctx.index, ctx.surface, &presentSupport);
  return presentSupport == VK_TRUE;
}
auto checkTransferQueue(const QueueFamilyCheckContext& ctx) -> bool {
  // 支持 graphics 和 compute operation 的 queue 也必定支持 transfer operation
  return ctx.properties.queueFlags &
         (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT);
}

auto createDevice(
  const PhysicalDeviceInfo& physical_device_info, std::span<const char*> required_extensions
) -> std::pair<Device, std::vector<std::pair<VkQueue, uint32_t>>> {
  /*
   * 1. 创建 VkDeviceQueueCreateInfo 数组, 用于指定 logic device 中的队列
   * 2. 根据 queue create info 和 deviceFeatures_ 创建 device create info
   *
   */

  auto create_meta_infos =
    toy::SortedView(
      physical_device_info.queue_indices | views::transform(&std::pair<uint32_t, uint32_t>::first)
    ) |
    toy::chunkBy(std::equal_to{}) | views::transform([](auto subrange) {
      std::vector<float> queue_priorities(subrange.size());
      ranges::fill(queue_priorities, 1.0);
      return std::tuple{ *subrange.begin(),
                         static_cast<uint32_t>(subrange.size()),
                         std::move(queue_priorities) };
    }) |
    ranges::to<std::vector>();
  std::vector<VkDeviceQueueCreateInfo> queue_create_infos =
    create_meta_infos | views::transform([](auto&& tuple) {
      return VkDeviceQueueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = std::get<0>(tuple),
        .queueCount = std::get<1>(tuple),
        .pQueuePriorities = std::get<2>(tuple).data(),
      };
    }) |
    ranges::to<std::vector>();

  VkDeviceCreateInfo create_info{
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
    .pQueueCreateInfos = queue_create_infos.data(),
    .enabledExtensionCount = static_cast<uint32_t>(required_extensions.size()),
    .ppEnabledExtensionNames = required_extensions.data(),
    .pEnabledFeatures = &physical_device_info.features,
  };

  // 旧实现在 (instance, physical device) 和 (logic device以上) 两个层面有不同的
  // layer, 而在新实现中合并了 不再需要定义 enabledLayerCount 和
  // ppEnabledLayerNames createInfo.enabledLayerCount =
  // static_cast<uint32_t>(requiredLayers_.size());
  // createInfo.ppEnabledLayerNames = requiredLayers_.data();

  auto device = Device{ physical_device_info.device, create_info };

  auto queues = physical_device_info.queue_indices |
                views::transform([device = device.get()](auto pair) {
                  VkQueue queue;
                  vkGetDeviceQueue(device, pair.first, pair.second, &queue);
                  return std::pair{ queue, pair.first };
                }) |
                ranges::to<std::vector>();
  return { std::move(device), queues };
}

} // namespace vk
