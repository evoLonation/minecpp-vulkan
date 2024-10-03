module render.vk.tracker;

namespace rd::vk {

auto BufferBarrierTracker::setNewScope(Scope scope, uint32 family)
  -> std::variant<std::monostate, BarrierRecorder, FamilyTransferRecorder> {
  toy::throwf(scope.stage_mask != 0, "scope.stage_mask must not is STAGE_NONE");
  auto type = checkAccessType(scope.access_mask);
  using enum AccessType;
  auto recorder = std::variant<std::monostate, BarrierRecorder, FamilyTransferRecorder>{};
  auto generateRecorder = [&](Scope src_scope) {
    if (_family == family) {
      recorder = [buffer = _buffer, src_scope, dst_scope = scope](VkCommandBuffer cmdbuf) {
        recordBufferBarrier(
          cmdbuf, buffer, BarrierScope{ src_scope, dst_scope }, FamilyTransferInfo{}
        );
      };
    } else {
      recorder = FamilyTransferRecorder{
        .release =
          [buffer = _buffer, src_scope, old_family = _family, family](VkCommandBuffer cmdbuf) {
            recordBufferBarrier(
              cmdbuf,
              buffer,
              BarrierScope::release(src_scope),
              FamilyTransferInfo{ old_family, family }
            );
          },
        .acquire =
          [buffer = _buffer, scope, old_family = _family, family](VkCommandBuffer cmdbuf) {
            recordBufferBarrier(
              cmdbuf, buffer, BarrierScope::acquire(scope), FamilyTransferInfo{ old_family, family }
            );
          },
      };
    }
  };
  if (type == WRITE) {
    // dep: if old reads exist, exedep to all old reads, else memdep to old write
    // old write = scope - read, clear old reads
    if (_last_read_stages != 0) {
      generateRecorder(Scope{
        .stage_mask = _last_read_stages,
        .access_mask = 0,
      });
    } else if (_last_write_scope.stage_mask != 0) {
      generateRecorder(_last_write_scope);
    }
    _last_read_stages = 0;
    _last_write_scope = scope;
    _last_write_scope.access_mask = extractWriteAccess(_last_write_scope.access_mask);
  } else if (type == READ) {
    if (_family == family) {
      // dep: memdep to old write
      // old_reads += scope.stage
      if (_last_write_scope.stage_mask != 0) {
        generateRecorder(_last_write_scope);
      }
      _last_read_stages |= scope.stage_mask;
    } else {
      // dep: first try dep to reads, then try dep to writes
      if (_last_read_stages != 0) {
        generateRecorder(Scope{
          .stage_mask = _last_read_stages,
          .access_mask = 0,
        });
      } else if (_last_write_scope.stage_mask != 0) {
        generateRecorder(_last_write_scope);
      }
      _last_write_scope = {};
      _last_read_stages = scope.stage_mask;
    }
  }
  _family = family;
  return recorder;
}

} // namespace rd::vk
