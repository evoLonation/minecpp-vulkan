module render.queue_requestor;

import toy;

import <vulkan_config.h>;

namespace rd {

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

auto QueueRequestor::checkPdevice(DeviceCapabilityBuilder& request)
  -> std::expected<void, std::string> {
  auto& pdevice = request.getPdevice();

  auto family_count = pdevice.getAllQueueFamilyProperties().size();
  auto requirement_count = _requirements.size();
  toy::debugf(
    "queue family count: {}, queue family requirement count: {}", family_count, requirement_count
  );

  std::vector<std::vector<int>> graph(requirement_count);

  for (auto [family_i, properties] : pdevice.getAllQueueFamilyProperties() | toy::enumerate) {
    // auto queue_count = static_cast<int>(properties.queueCount);
    toy::debugf("check queue family {}, which has {} queues", family_i, properties.queueCount);
    for (auto [requirement_i, requirement] : _requirements | toy::enumerate) {
      // auto& [queue_checker, queue_number] = queue_requirement;
      if (properties.queueCount >= requirement.queue_count &&
          requirement.checker(QueueFamilyCheckContext{ pdevice.get(), family_i, properties })) {
        graph[requirement_i].push_back(family_i);
        toy::debugf("queue request {} success", requirement_i);
      } else {
        toy::debugf("queue request {} failed", requirement_i);
      }
    }
  }
  if (auto res = hungarian(graph, family_count); res.has_value()) {
    for (auto [index, family_i] : res.value() | toy::enumerate) {
      auto family_queue_count =
        FamilyQueueCount{ static_cast<uint32>(family_i), _requirements[index].queue_count };
      _family_infos[pdevice.get()].emplace_back(_requirements[index].family, family_queue_count);
      request.family_queue_counts.push_back(family_queue_count);
    }
    return {};
  } else {
    return std::unexpected("queue request failed");
  }
}
} // namespace rd