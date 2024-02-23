module vulkan.device;

import "vulkan_config.h";
import vulkan.tool;
import toy;

namespace vk {

auto pickPhysicalDevice(VkInstance                          instance,
                        VkSurfaceKHR                        surface,
                        std::span<const char*>              required_extensions,
                        DeviceChecker                       device_checker,
                        SurfaceChecker                      surface_checker,
                        std::span<const QueueFamilyChecker> queue_chekers)
  -> PhysicalDeviceInfo {
  std::vector<VkPhysicalDevice> devices =
    getVkResources(vkEnumeratePhysicalDevices, instance);
  std::vector<PhysicalDeviceInfo> supported_devices;
  for (auto device : devices) {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties(device, &device_properties);
    VkPhysicalDeviceFeatures device_features;
    vkGetPhysicalDeviceFeatures(device, &device_features);
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &capabilities);
    auto presentModes = getVkResources(
      vkGetPhysicalDeviceSurfacePresentModesKHR, device, surface);
    auto formats =
      getVkResources(vkGetPhysicalDeviceSurfaceFormatsKHR, device, surface);

    toy::debugf("checking physical device {}:", device_properties.deviceName);
    bool unsupport = false;
    if (!device_checker(
          DeviceCheckContext{ device_properties, device_features })) {
      unsupport = true;
    }
    auto selected_surface = surface_checker(
      SurfaceCheckContext{ capabilities, presentModes, formats });
    if (!selected_surface.has_value()) {
      unsupport = true;
    }

    try {
      checkAvaliableSupports(
        required_extensions,
        getVkResources(vkEnumerateDeviceExtensionProperties, device, nullptr),
        [](auto& extension) { return extension.extensionName; });
    } catch (const std::exception& e) {
      toy::debug(e.what());
      unsupport = true;
    }

    auto queue_family_indices =
      getQueueFamilyIndices(device, surface, queue_chekers);
    if (!queue_family_indices.has_value()) {
      unsupport = true;
    }

    if (!unsupport) {
      supported_devices.push_back(
        { .device = device,
          .properties = device_properties,
          .features = device_features,
          .capabilities = capabilities,
          .present_mode = selected_surface->present_mode,
          .surface_format = selected_surface->surface_format,
          .queue_indices = queue_family_indices.value() });
    }
  }
  if (supported_devices.empty()) {
    toy::throwf("no support physical device");
  }
  toy::debugf("support devices: {::}",
              supported_devices | views::transform([](auto& info) {
                return info.properties.deviceName;
              }));
  auto selected_device = supported_devices[0];
  toy::debugf("select device {}", selected_device.properties.deviceName);
  return selected_device;
}

auto getQueueFamilyIndices(VkPhysicalDevice                    device,
                           VkSurfaceKHR                        surface,
                           std::span<const QueueFamilyChecker> queue_chekers)
  -> std::optional<QueueIndexes> {

  auto queue_families =
    getVkResources(vkGetPhysicalDeviceQueueFamilyProperties, device);

  auto family_size = queue_families.size();
  auto request_size = queue_chekers.size();
  toy::debugf(
    "queue family size: {}, queue request size: {}", family_size, request_size);

  // 二分图，左半边是所有请求，右半边是所有队列(将队列族展开)
  std::vector<int> visit(request_size);
  std::fill(visit.begin(), visit.end(), -1);

  std::vector<std::vector<int>> queue2request(family_size);
  for (auto&& [properties, vector] :
       ranges::zip_view(queue_families, queue2request)) {
    vector = std::vector<int>(properties.queueCount);
    std::fill(vector.begin(), vector.end(), -1);
  }
  QueueIndexes request2family(request_size);

  std::vector<std::vector<std::pair<int, int>>> graph(request_size);

  for (auto [family_i, properties] : queue_families | toy::enumerate) {
    int request_i = 0;
    int queue_number = properties.queueCount;
    toy::debugf(
      "check queue family {}, which has {} queues", family_i, queue_number);
    for (auto&& [request_i, queue_checker] : queue_chekers | toy::enumerate) {
      if (queue_checker(
            QueueFamilyCheckContext{ device, surface, family_i, properties })) {
        graph[request_i].append_range(views::zip(
          views::repeat(family_i, queue_number), views::iota(0, queue_number)));
        toy::debugf("queue request {} success", request_i);
      } else {
        toy::debugf("queue request {} failed", request_i);
      }
    }
  }

  std::function<bool(int, int)> dfs =
    [&graph, &visit, &queue2request, &request2family, &dfs](int u,
                                                            int tag) -> bool {
    toy::debugf("u: {}", u);
    if (visit[u] == tag) {
      toy::debugf("visit[u] == tag, return false");
      return false;
    }
    visit[u] = tag;
    for (auto [family_i, queue_i] : graph[u]) {
      toy::debugf("u {} lookup {}", u, std::pair{ family_i, queue_i });
      if ((queue2request[family_i][queue_i] == -1 ||
           dfs(queue2request[family_i][queue_i], tag))) {
        queue2request[family_i][queue_i] = u;
        request2family[u] = { family_i, queue_i };
        toy::debugf("u {} select {}", u, std::pair{ family_i, queue_i });
        return true;
      }
    }
    toy::debugf("u {} no satisfied select", u);
    return false;
  };

  if (!ranges::all_of(views::iota((decltype(request_size))0, request_size),
                      [&dfs](int u) { return dfs(u, u); })) {
    return std::nullopt;
  }

  return request2family;
}

auto checkPhysicalDeviceSupport(const DeviceCheckContext& ctx) -> bool {
  return toy::checkDebugf(
           ctx.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
           "device not satisfied VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU") &&
         toy::checkDebugf(ctx.features.geometryShader == VK_TRUE,
                          "device not support geometryShader feature");
}

auto checkSurfaceSupport(const SurfaceCheckContext& ctx)
  -> std::optional<SelectedSurfaceInfo> {
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
  auto p_present_mode =
    ranges::find(ctx.present_modes, VK_PRESENT_MODE_FIFO_KHR);
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
  vkGetPhysicalDeviceSurfaceSupportKHR(
    ctx.device, ctx.index, ctx.surface, &presentSupport);
  return presentSupport == VK_TRUE;
}
auto checkTransferQueue(const QueueFamilyCheckContext& ctx) -> bool {
  // 支持 graphics 和 compute operation 的 queue 也必定支持 transfer operation
  return ctx.properties.queueFlags &
         (VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT);
}

auto createDevice(const PhysicalDeviceInfo& physical_device_info,
                  std::span<const char*>    required_extensions)
  -> std::pair<Device, std::vector<VkQueue>> {
  /*
   * 1. 创建 VkDeviceQueueCreateInfo 数组, 用于指定 logic device 中的队列
   * 2. 根据 queue create info 和 deviceFeatures_ 创建 device create info
   *
   */

  auto create_meta_infos =
    toy::SortedView(physical_device_info.queue_indices |
                    views::transform(&std::pair<uint32_t, uint32_t>::first)) |
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
                  return queue;
                }) |
                ranges::to<std::vector>();
  return { std::move(device), queues };
}

void destroyLogicalDevice(VkDevice device) noexcept {
  vkDestroyDevice(device, nullptr);
}

} // namespace vk
