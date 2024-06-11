module render.vk.executor;

import "vulkan_config.h";
import render.vk.tool;
import render.vk.resource;
import render.vk.surface;
import render.vk.device;
import render.vk.sync;
import render.vk.command;

import std;
import toy;

namespace rd::vk {

/**
 * @brief 二分图匹配算法
 */
auto hungarian(std::span<const std::vector<int>> graph, int right_count)
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

struct QueueFamilyCheckContext {
  VkPhysicalDevice        device;
  VkSurfaceKHR            surface;
  size_t                  index;
  VkQueueFamilyProperties properties;
};
/**
 * @brief first: queue number, second: queue checker
 */
using QueueRequest = std::pair<int, std::function<bool(const QueueFamilyCheckContext&)>>;

auto getQueueFamilyIndices(const PdeviceContext& ctx, std::span<const QueueRequest> queue_requests)
  -> std::optional<std::vector<uint32_t>> {

  auto queue_families = getVkResources(vkGetPhysicalDeviceQueueFamilyProperties, ctx.device);

  auto family_size = queue_families.size();
  auto request_size = queue_requests.size();
  toy::debugf("queue family size: {}, queue request size: {}", family_size, request_size);

  std::vector<std::vector<int>> graph(request_size);

  for (auto [family_i, properties] : queue_families | toy::enumerate) {
    auto queue_count = (int)properties.queueCount;
    toy::debugf("check queue family {}, which has {} queues", family_i, queue_count);
    for (auto [request_i, queue_request] : queue_requests | toy::enumerate) {
      auto& [queue_number, queue_checker] = queue_request;
      if (properties.queueCount >= queue_number &&
          queue_checker(QueueFamilyCheckContext{
            ctx.device, Surface::getInstance(), family_i, properties })) {
        graph[request_i].push_back(family_i);
        toy::debugf("queue request {} success", request_i);
      } else {
        toy::debugf("queue request {} failed", request_i);
      }
    }
  }

  return hungarian(graph, queue_families.size()).transform([&](auto&& results) {
    return results | views::transform([](int right) { return (uint32_t)right; }) |
           ranges::to<std::vector>();
  });
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

auto CommandExecutor::checkPdevice(const PdeviceContext& ctx) -> bool {
  auto queue_requests = std::array{
    QueueRequest{ worker_count * 2 + 1, vk::checkGraphicQueue },
    QueueRequest{ 2, vk::checkPresentQueue },
    QueueRequest{ 1, vk::checkTransferQueue },
  };
  if (auto res = getQueueFamilyIndices(ctx, queue_requests); res.has_value()) {
    _queue_meta.append_range(
      views::zip(queue_requests, res.value()) |
      views::transform([](auto pair) { return std::pair{ pair.second, pair.first.first }; })
    );
    return true;
  } else {
    return false;
  }
}

} // namespace rd::vk