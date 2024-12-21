module;
#include <toy.h>
module engine.base.reactive;

namespace eg {

RevalBase::~RevalBase() { reset(); }

void RevalBase::reset() {
  for (auto& bind : _to_binds) {
    for (auto* reval : bind->to_revals) {
      if (reval->_dirty_bind == bind.get()) {
        reval->update();
      }
    }
  }
  for (auto& bind : _from_binds) {
    for (auto* reval : bind->to_revals) {
      if (reval->_dirty_bind == bind.get()) {
        reval->update();
      }
    }
  }
  for (auto& bind : _to_binds) {
    for (auto* reval : bind->to_revals) {
      reval->_from_binds.erase(bind);
    }
    for (auto* reval : bind->from_revals) {
      if (reval != this) {
        reval->_to_binds.erase(bind);
      }
    }
  }
  for (auto& bind : _from_binds) {
    for (auto* reval : bind->from_revals) {
      reval->_to_binds.erase(bind);
    }
    for (auto* reval : bind->to_revals) {
      if (reval != this) {
        reval->_from_binds.erase(bind);
      }
    }
  }
  _to_binds.clear();
  _from_binds.clear();
  _dirty_bind = nullptr;
}

void RevalBase::update() {
  // todo: better algorithm to Topological Sorting
  if (_track) {
    toy::debugf("start update \"{}\"", _name);
  }
  auto queue = std::queue<RevalBase*>{};
  queue.push(this);
  auto stack = std::stack<std::pair<Bind*, std::vector<RevalBase*>>>{};
  while (!queue.empty()) {
    auto e = queue.front();
    queue.pop();
    auto bind = e->_dirty_bind;
    if (!bind) {
      continue;
    }
    TOY_ASSERT(e->_from_binds.contains(bind->shared_from_this()));
    // just compute friends which _dirty_bind == bind
    auto es_to = bind->to_revals;
    for (auto& to : es_to) {
      if (to->_dirty_bind != bind) {
        to = nullptr;
      }
    }
    if (_track) {
      toy::debugf(
        "push compute {} -> {}",
        bind->from_revals | views::transform([](auto* e) { return e->_name; }),
        es_to | views::transform([](auto* e) { return e ? e->_name : "null"; })
      );
    }
    stack.push({ bind, std::move(es_to) });
    for (auto* dep : bind->from_revals) {
      queue.push(dep);
    }
  }

  auto computed = std::unordered_set<Bind*>{};
  while (!stack.empty()) {
    auto [bind, to_revals] = stack.top();
    stack.pop();
    if (computed.contains(bind)) {
      continue;
    }
    computed.insert(bind);
    for (auto& to : to_revals) {
      if (to) {
        to->_dirty_bind = nullptr;
      }
    }
    bind->computer(bind->from_revals, to_revals);
  }
}

void RevalBase::spread() {
  if (_track) {
    toy::debugf("start spread \"{}\"", _name);
  }
  auto queue = std::queue<RevalBase*>{};
  this->_dirty_bind = nullptr;
  queue.push(this);
  while (!queue.empty()) {
    auto reval = queue.front();
    queue.pop();
    // avoid bidirectional binds
    auto to_binds = std::vector<Bind*>{};
    to_binds.append_range(reval->_to_binds | views::transform([](auto& bind) {
                            return bind.get();
                          }));
    if (reval->_dirty_bind) {
      auto iter =
        ranges::find_if(to_binds, ([&](auto* bind) { return bind->dual == reval->_dirty_bind; }));
      if (iter != to_binds.end()) {
        to_binds.erase(iter);
      }
    }
    // find all bidirectional binds and update friends
    for (auto* bind : to_binds) {
      if (!bind->isBidirectional()) {
        continue;
      }
      // friends: es_from
      // reval not need to update because we will change it
      // just need to update whose _dirty_bind == bind.dual
      for (auto* from : bind->from_revals) {
        if (from != reval && from->_dirty_bind == bind->dual) {
          from->update();
        }
      }
    }
    for (auto* bind : to_binds) {
      if (_track) {
        toy::debugf(
          "push dependency {} -> {}",
          bind->to_revals | views::transform([](auto* e) { return e->_name; }),
          bind->from_revals | views::transform([](auto* e) { return e->_name; })
        );
      }
      for (auto* to : bind->to_revals) {
        to->_dirty_bind = bind;
        queue.push(to);
      }
    }
  }
}

void RevalBase::bind(std::shared_ptr<Bind> bind) {
  TOY_ASSERT(bind->dual == nullptr);
  // check bidirectional bind
  // find a: a._es_to == bind._es_from, a._es_from == bind._es_to
  for (auto& candidate : bind->from_revals[0]->_from_binds) {
    if (candidate->from_revals == bind->to_revals && candidate->to_revals == bind->from_revals) {
      toy::debugf(
        "find bidirectional bind: {} <-> {}",
        bind->from_revals | views::transform([](auto* e) { return e->_name; }),
        bind->to_revals | views::transform([](auto* e) { return e->_name; })
      );
      bind->dual = candidate.get();
      candidate->dual = bind.get();
      break;
    }
  }
  for (auto* from_r : bind->from_revals) {
    from_r->_to_binds.insert(bind);
  }
  for (auto* to : bind->to_revals) {
    to->_from_binds.insert(bind);
  }
}

void RevalBase::unbind(std::shared_ptr<Bind> bind) {
  for (auto* to : bind->to_revals) {
    if (to->_dirty_bind == bind.get()) {
      to->update();
    }
  }
  if (bind->dual) {
    bind->dual->dual = nullptr;
  }
  for (auto* from : bind->from_revals) {
    from->_to_binds.erase(bind);
  }
  for (auto* to : bind->to_revals) {
    to->_from_binds.erase(bind);
  }
}

} // namespace eg