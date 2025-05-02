import toy;
import std;
import render.tracker2;
import render.sync;
import <vulkan_config.h>;
import render.execution;
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

TEST(Submitter) {
  // init contexts
  auto instance_extensions = std::vector<std::string>{};
  auto instance = std::make_unique<rd::InstanceResource>("test submitter", instance_extensions);
  using namespace std::placeholders;
  auto queue_builder = QueueManagerBuilder{
    std::vector<QueueFamilyRequirement>{
      QueueFamilyRequirement{
        .family = FamilyType::GRAPHICS,
        .queue_count = 1,
        // .queue_count = 2,
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
  auto device = std::make_unique<rd::Device>(rd::Device::create(device_checkers));
  auto queue_manager = queue_builder.build();
  auto cmdbuf_manager = CmdbufManager{};
  auto sema_pool = SemaphorePool{};
  auto worker = Execution::Worker{};

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
  auto queue = queue_manager.getQueue(FamilyType::TRANSFER, 0);
  auto buffer = rd::Buffer{ 8, VK_BUFFER_USAGE_2_TRANSFER_DST_BIT, VkMemoryPropertyFlags{} };
  auto submitter = SubmitterAutoSync{ queue };
  auto cmdbuf = submitter.addCommandBuffer();
  auto syner = Synchronizer{ buffer };
  submitter.sync(syner, write_scope1);
  submitter.sync(syner, read_scope1);
  submitter.sync(syner, read_scope2);
  submitter.sync(syner, write_scope2);
  submitter.submit();
}