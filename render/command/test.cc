import toy;
import std;
import render.semaphore;
import render.tracker2;
import render.sync;
import <vulkan_config.h>;
import render.execution;
import render.submitter2;
import render.instance;
import render.device;
import render.queue;
import render.sampler;
import render.cmdbuf;
import render.buffer;

#include <test.h>
#include <toy.h>

using namespace rd;
using namespace rd::exec;
TEST(Tracker) {
  auto write_scope1 = Scope{
    .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
  };
  auto write_scope2 = Scope{
    .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
  };
  auto read_scope1 = Scope{
    .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
    .access_mask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
  };
  auto read_scope2 = Scope{
    .stage_mask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
    .access_mask = VK_ACCESS_2_INDEX_READ_BIT,
  };
  // 测试刚创建时状态都为空
  {
    auto tracker = ScopeTracker{};
    TOY_ASSERT(!tracker.getAllNeedSync());
    TOY_ASSERT(!tracker.getReadNeedSync());
  }
  // 测试刚创建时分别进行读和写操作
  {
    auto tracker = ScopeTracker{};
    tracker.updateScope(write_scope1);
    TOY_ASSERT_EQ(*tracker.getAllNeedSync(), write_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(), write_scope1.extractWriteAccess());
  }
  {
    auto tracker = ScopeTracker{};
    tracker.updateScope(read_scope1);
    tracker.updateScope(read_scope2);
    TOY_ASSERT_EQ(*tracker.getAllNeedSync(), (read_scope1 | read_scope2).extractWriteAccess());
    TOY_ASSERT(!tracker.getReadNeedSync());
  }
  // 测试一连串同步操作
  {
    auto tracker = ScopeTracker{};
    // 写 -> 写
    tracker.updateScope(write_scope1);
    TOY_ASSERT_EQ(*tracker.getNeedSync(write_scope2), write_scope1.extractWriteAccess());
    tracker.updateScope(write_scope2);
    // 写 -> 读++
    TOY_ASSERT_EQ(*tracker.getNeedSync(read_scope1), write_scope2.extractWriteAccess());
    tracker.updateScope(read_scope1);
    TOY_ASSERT_EQ(*tracker.getNeedSync(read_scope2), write_scope2.extractWriteAccess());
    tracker.updateScope(read_scope2);
    // 读++ -> 写
    TOY_ASSERT_EQ(
      *tracker.getNeedSync(write_scope1), (read_scope1 | read_scope2).extractWriteAccess()
    );
    tracker.updateScope(write_scope1);
    TOY_ASSERT_EQ(*tracker.getWriteNeedSync(), write_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(), write_scope1.extractWriteAccess());
    // setNeedSync
    for (auto scope : std::vector{ write_scope1, write_scope2, read_scope1, read_scope2 }) {
      tracker.setNeedSync(scope);
      TOY_ASSERT_EQ(*tracker.getWriteNeedSync(), scope.extractWriteAccess());
      TOY_ASSERT_EQ(*tracker.getReadNeedSync(), scope.extractWriteAccess());
    }
  }
}

TEST(ImageTracker) {
  auto write_scope1 = ImageScope{
    .scope =
      Scope{
        .stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .access_mask =
          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
      },
    .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  auto write_scope2 = ImageScope{
    .scope =
      Scope{
        .stage_mask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
      },
    .layout = VK_IMAGE_LAYOUT_GENERAL,
  };
  auto read_scope1 = ImageScope{
    .scope =
      Scope{
        .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
        .access_mask = VK_ACCESS_2_SHADER_READ_BIT,
      },
    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  // scope that layout is same as read_scope1
  auto read_scope1_1 = ImageScope{
    .scope =
      Scope{
        .stage_mask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .access_mask = VK_ACCESS_2_SHADER_READ_BIT,
      },
    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  auto read_scope2 = ImageScope{
    .scope =
      Scope{
        .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .access_mask = VK_ACCESS_2_TRANSFER_READ_BIT,
      },
    .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  };
  auto init_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  // layout that diff with any layouts
  auto diff_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  // 测试刚创建时状态都为空
  {
    auto tracker = ImageScopeTracker{};
    TOY_ASSERT(!tracker.getAllNeedSync(init_layout));
    TOY_ASSERT(!tracker.getReadNeedSync(init_layout));
    TOY_ASSERT_EQ(*tracker.getAllNeedSync(diff_layout), ImageScope());
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(diff_layout), ImageScope());
  }
  // 测试刚创建时分别进行读和写操作
  {
    auto tracker = ImageScopeTracker{};
    tracker.updateScope(write_scope1);
    TOY_ASSERT_EQ(*tracker.getAllNeedSync(write_scope1.layout), write_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getAllNeedSync(diff_layout), write_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(write_scope1.layout), write_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(diff_layout), write_scope1.extractWriteAccess());
  }
  {
    // 连续读两个相同的layout，再读一个不同的layout
    auto tracker = ImageScopeTracker{};
    tracker.updateScope(read_scope1);
    TOY_ASSERT_EQ(*tracker.getAllNeedSync(read_scope1.layout), read_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getAllNeedSync(diff_layout), read_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(read_scope1.layout), read_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(diff_layout), read_scope1.extractWriteAccess());
    tracker.updateScope(read_scope1_1);
    TOY_ASSERT_EQ(*tracker.getAllNeedSync(read_scope1.layout), read_scope1_1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getAllNeedSync(diff_layout), read_scope1_1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(read_scope1.layout), read_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(diff_layout), read_scope1_1.extractWriteAccess());
  }
  // 测试一连串同步操作
  {
    auto tracker = ImageScopeTracker{};
    // 写 -> 写
    tracker.updateScope(write_scope1);
    TOY_ASSERT_EQ(*tracker.getNeedSync(write_scope2), write_scope1.extractWriteAccess());
    tracker.updateScope(write_scope2);
    // 写 -> 读++(layout相同)
    TOY_ASSERT_EQ(*tracker.getNeedSync(read_scope1), write_scope2.extractWriteAccess());
    tracker.updateScope(read_scope1);
    TOY_ASSERT_EQ(*tracker.getNeedSync(read_scope1_1), read_scope1.extractWriteAccess());
    tracker.updateScope(read_scope1_1);
    TOY_ASSERT_EQ(*tracker.getNeedSync(read_scope1), read_scope1.extractWriteAccess());
    tracker.updateScope(read_scope1);
    // 读(layout相同) -> 读(layout不同)
    auto merged_scope = ImageScope{ read_scope1.scope | read_scope1_1.scope, read_scope1.layout };
    TOY_ASSERT_EQ(*tracker.getNeedSync(read_scope2), merged_scope.extractWriteAccess());
    tracker.updateScope(read_scope2);
    // 读 -> 写
    TOY_ASSERT_EQ(*tracker.getNeedSync(write_scope1), read_scope2.extractWriteAccess());
    tracker.updateScope(write_scope1);
    TOY_ASSERT_EQ(*tracker.getWriteNeedSync(diff_layout), write_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(
      *tracker.getWriteNeedSync(write_scope1.layout), write_scope1.extractWriteAccess()
    );
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(diff_layout), write_scope1.extractWriteAccess());
    TOY_ASSERT_EQ(*tracker.getReadNeedSync(write_scope1.layout), write_scope1.extractWriteAccess());
    // setNeedSync
    for (auto scope : std::vector{ write_scope1, write_scope2, read_scope1, read_scope2 }) {
      tracker.setNeedSync(scope);
      TOY_ASSERT_EQ(*tracker.getWriteNeedSync(scope.layout), scope.extractWriteAccess());
      TOY_ASSERT_EQ(*tracker.getWriteNeedSync(diff_layout), scope.extractWriteAccess());
      TOY_ASSERT_EQ(*tracker.getReadNeedSync(scope.layout), scope.extractWriteAccess());
      TOY_ASSERT_EQ(*tracker.getReadNeedSync(diff_layout), scope.extractWriteAccess());
    }
  }
  // resetScope(layout): 通常用于使用了sema或者fence进行同步后，虽然无须通过barrier同步，但是需要进行layout trasition
  {
    auto tracker = ImageScopeTracker{};
    tracker.updateScope(write_scope1);
    tracker.updateScope(read_scope1);
    tracker.resetScope(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    TOY_ASSERT(!tracker.getAllNeedSync(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
    TOY_ASSERT(!tracker.getReadNeedSync(VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
    TOY_ASSERT_EQ(
      *tracker.getAllNeedSync(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
      ImageScope(Scope(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    );
    TOY_ASSERT_EQ(
      *tracker.getReadNeedSync(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
      ImageScope(Scope(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    );
  }
}

auto createDeviceAndQueueManager(uint32 graphics_queue_count) -> toy::Expected<std::any> {
  auto queue_builder = QueueManagerBuilder{
    std::vector<QueueFamilyRequirement>{
      QueueFamilyRequirement{
        .family = FamilyType::GRAPHICS,
        .queue_count = graphics_queue_count,
        .checker = getGraphicQueueChecker(),
      },
      QueueFamilyRequirement{
        .family = FamilyType::TRANSFER,
        .queue_count = 1,
        .checker = getTransferQueueChecker(),
      },
    },
  };
  auto device_checkers = std::vector<rd::DeviceCapabilityChecker>{
    [&](auto& ctx) { return queue_builder.checkPdevice(ctx); },
    rd::device_checkers::sync,
  };
  try {
    auto device = std::make_shared<rd::Device>(rd::Device::create(device_checkers));
    auto queue_manager = std::make_shared<rd::exec::QueueManager>(queue_builder.build());
    return std::any{ std::tuple{
      std::move(device),
      std::move(queue_manager),
    } };
  } catch (const std::exception& e) {
    return toy::unexpected(e.what());
  }
}

TEST(BufferSynchronizer) {
  // init contexts
  auto instance_extensions = std::vector<std::string>{};
  auto instance = std::make_unique<rd::InstanceResource>("test submitter", instance_extensions);
  using namespace std::placeholders;

  auto has_multi_queue = true;
  auto device_and_queue_manager = createDeviceAndQueueManager(2);
  if (!device_and_queue_manager) {
    toy::debugf(
      "Failed to create device and queue manager with 2 graphics queues, try create 1 queue and "
      "multi queue test won't run: \n{}",
      device_and_queue_manager.error()
    );
    has_multi_queue = false;
    device_and_queue_manager = createDeviceAndQueueManager(1);
    if (!device_and_queue_manager.has_value()) {
      toy::throwf("{}", device_and_queue_manager.error());
    }
  }
  auto& queue_manager = rd::exec::QueueManager::getInstance();
  auto  cmdbuf_manager = CmdbufManager{};
  auto  sema_pool = SemaphorePool{};
  auto  worker = Execution::Worker{};

  // todo: add assert to test
  auto write_scope1 = Scope{
    .stage_mask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    .access_mask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT,
  };
  auto write_scope2 = Scope{
    .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
    .access_mask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
  };
  auto read_scope1 = Scope{
    .stage_mask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT,
    .access_mask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
  };
  auto read_scope2 = Scope{
    .stage_mask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
    .access_mask = VK_ACCESS_2_INDEX_READ_BIT,
  };

  auto transfer_queue = queue_manager.getQueue(FamilyType::TRANSFER, 0);
  auto graphics_queue = queue_manager.getQueue(FamilyType::GRAPHICS, 0);
  auto buffer = rd::Buffer{ 8, VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, VkMemoryPropertyFlags{} };
  auto syner = BufferSynchronizer{ buffer.get() };
  // multi sync in one submitter
  toy::debugf("multi sync in one submitter");
  {
    auto submitter = SubmitterAutoSync{ transfer_queue };
    submitter.sync(syner, write_scope1);
    submitter.sync(syner, read_scope1);
    submitter.sync(syner, read_scope2);
    submitter.sync(syner, write_scope2);
    submitter.submit();
  }
  // across family sync
  toy::debugf("across family sync");
  {
    auto submitter = SubmitterAutoSync{ graphics_queue };
    submitter.sync(syner, write_scope1);
    submitter.submit();
  }
  // same queue sync (same as multi sync in one submitter)
  toy::debugf("same queue sync");
  {
    auto submitter = SubmitterAutoSync{ graphics_queue };
    submitter.sync(syner, read_scope1);
    submitter.sync(syner, read_scope2);
    submitter.sync(syner, write_scope2);
    submitter.submit();
  }
  // insert a syncHost between two submitters
  toy::debugf("insert a syncHost between two submitters");
  {
    syner.syncHost(AccessType::READ | AccessType::WRITE);
    auto submitter = SubmitterAutoSync{ graphics_queue };
    submitter.sync(syner, write_scope1);
    submitter.submit();
  }
  {
    syner.syncHost(AccessType::READ | AccessType::WRITE);
    auto submitter = SubmitterAutoSync{ transfer_queue };
    submitter.sync(syner, write_scope1);
    submitter.submit().hostWait();
  }
}