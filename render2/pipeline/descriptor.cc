module render.vk.descriptor;

import std;

import render.vk.device;

import <vulkan_config.h>;

namespace rd::vk {

DescriptorSetLayout::DescriptorSetLayout(std::vector<BindingInfo> infos)
  : _infos(std::move(infos)) {
  auto bindings = std::vector<VkDescriptorSetLayoutBinding>{};
  for (auto [index, info] : _infos | toy::enumerate) {
    bindings.push_back(VkDescriptorSetLayoutBinding{
      .binding = index,
      .descriptorType = info.type,
      .descriptorCount = info.count,
      .stageFlags = info.stage,
      .pImmutableSamplers = nullptr,
    });
  }
  rs::DescriptorSetLayout::operator=(VkDescriptorSetLayoutCreateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .bindingCount = static_cast<uint32>(_infos.size()),
    .pBindings = bindings.data(),
  });
}

void DescriptorSet::addWaitable(std::shared_ptr<Waitable> waitable) {
  removeIdles();
  _waitables.push_back(std::move(waitable));
}

auto DescriptorSet::waitIdle(uint64 nano_timeout) -> bool {
  auto res = Waitable::wait(
    _waitables | views::transform([](auto& w) {
      return std::pair{ w.get(), VkPipelineStageFlags2{ VK_PIPELINE_STAGE_ALL_COMMANDS_BIT } };
    }) |
      ranges::to<std::vector>(),
    false,
    nano_timeout
  );
  if (res) {
    _waitables.clear();
  } else {
    removeIdles();
  }
  return res;
}

void DescriptorSet::removeIdles() {
  auto new_end = std::remove_if(_waitables.begin(), _waitables.end(), [](auto& w) {
    return w->wait(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0);
  });
  _waitables.erase(new_end, _waitables.end());
}

void DescriptorSet::beforeDestroy() {
  if (valid()) {
    waitIdle();
  }
}

DescriptorPool::DescriptorPool(std::vector<BindingInfo> infos)
  : DescriptorSetLayout(std::move(infos)) {
  _pool_sizes = getInfo() | views::transform([](auto info) {
                  return VkDescriptorPoolSize{
                    .type = info.type,
                    .descriptorCount = info.count,
                  };
                }) |
                ranges::to<std::vector>();
  for (auto& pool_size : _pool_sizes) {
    pool_size.descriptorCount *= _allocate_count;
  }
  _pool_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .maxSets = _allocate_count,
    .poolSizeCount = static_cast<uint32>(_pool_sizes.size()),
    .pPoolSizes = _pool_sizes.data(),
  };
  _dset_layouts.append_range(views::repeat(get()) | views::take(_allocate_count));
}

void DescriptorPool::recycle(DescriptorSet dset) { _working_resources.push_back(std::move(dset)); }

auto DescriptorPool::extract() -> DescriptorSet {
  auto  pool_handle = _pool_counts.extract(_pool_counts.begin()).value().second;
  auto& dsets = _resources.at(pool_handle).second;
  auto  dset = dsets.exit();
  _pool_counts.emplace(dsets.size(), dsets.getPool());
  return { std::move(dset), this };
}

auto DescriptorPool::isEmpty() -> bool {
  return _pool_counts.empty() || _pool_counts.begin()->first == 0;
}

void DescriptorPool::expand() {
  auto pool = rs::DescriptorPool{ _pool_info };
  auto dsets = rs::DescriptorSets{ VkDescriptorSetAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = pool,
    .descriptorSetCount = static_cast<uint32>(_dset_layouts.size()),
    .pSetLayouts = _dset_layouts.data(),
  } };
  auto handle = pool.get();
  _pool_counts.emplace(_allocate_count, handle);
  _resources.emplace(handle, std::pair{ std::move(pool), std::move(dsets) });
}

void DescriptorPool::tryShrink() {
  auto idle_pool_num = 3;
  if (_pool_counts.size() <= idle_pool_num) {
    return;
  }
  auto iter = _pool_counts.begin();
  std::advance(iter, idle_pool_num);
  while (iter != _pool_counts.end() && iter->first == _allocate_count) {
    _resources.extract(iter->second);
    iter = _pool_counts.erase(iter);
  }
}

void DescriptorPool::workingToIdle() {
  auto new_end =
    std::remove_if(_working_resources.begin(), _working_resources.end(), [](auto& dset) {
      return dset.waitIdle(0);
    });
  for (auto& dset : ranges::subrange{ new_end, _working_resources.end() }) {
    auto& dsets = _resources.at(dset.getPool()).second;
    _pool_counts.extract(std::pair{ dsets.size(), dsets.getPool() });
    dsets.join(std::move(dset));
    _pool_counts.emplace(dsets.size(), dsets.getPool());
  }
  _working_resources.erase(new_end, _working_resources.end());
}

ResourceSet::ResourceSet(DescriptorPool* pool, std::initializer_list<ResourceBinding> bindings)
  : Base{ pool } {
  auto write_infos = std::vector<VkWriteDescriptorSet>{};
  auto all_image_infos = std::vector<std::vector<VkDescriptorImageInfo>>{};
  auto all_buffer_infos = std::vector<std::vector<VkDescriptorBufferInfo>>{};
  for (auto [binding_i, binding] : bindings | toy::enumerate) {
    for (auto [array_i, resource] : binding.resources | toy::enumerate) {
      _resources.push_back({ binding_i, array_i, resource });
    }
    auto resource_ctxs = std::vector<DescriptorResource::Context>{};
    for (auto* r : binding.resources) {
      resource_ctxs.push_back(r->getDescriptorContext());
    }
    auto write_info = VkWriteDescriptorSet{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = get(),
      .dstBinding = binding_i,
      // 数组起始索引
      .dstArrayElement = 0,
      .descriptorCount = static_cast<uint32>(resource_ctxs.size()),
      .descriptorType = pool->getInfo()[binding_i].type,
    };
    using BufferContext = DescriptorResource::BufferContext;
    using ImageContext = DescriptorResource::ImageContext;
    if (ranges::all_of(resource_ctxs, [](auto& x) {
          return std::holds_alternative<BufferContext>(x);
        })) {
      auto buffer_infos = std::vector<VkDescriptorBufferInfo>{};
      for (auto& ctx : resource_ctxs) {
        buffer_infos.push_back(std::get<BufferContext>(ctx).dscriptor_info);
      }
      write_info.pBufferInfo = buffer_infos.data();
      all_buffer_infos.push_back(std::move(buffer_infos));
    } else if (ranges::all_of(resource_ctxs, [](auto& x) {
                 return std::holds_alternative<ImageContext>(x);
               })) {
      auto image_infos = std::vector<VkDescriptorImageInfo>{};
      for (auto& ctx : resource_ctxs) {
        image_infos.push_back(std::get<ImageContext>(ctx).dscriptor_info);
      }
      write_info.pImageInfo = image_infos.data();
      all_image_infos.push_back(std::move(image_infos));
    } else {
      toy::throwf("different resource type in same binding");
    }
    write_infos.push_back(write_info);
  }
  vkUpdateDescriptorSets(Device::getInstance(), write_infos.size(), write_infos.data(), 0, nullptr);
}

} // namespace rd::vk