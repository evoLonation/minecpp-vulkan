import std;
import toy;

class Promise;

class Generator : public std::coroutine_handle<Promise> {
public:
  using promise_type = Promise;
  int next();
};
class Awaiter {
public:
  Awaiter& operator co_await() { return *this; }

  auto await_ready() -> bool {
    toy::debug("await_ready");
    return false;
  }
  auto await_suspend(std::coroutine_handle<Promise>) -> bool {
    toy::debug("await_suspend");
    return true;
  }
  void await_resume() {
    toy::debug("await_resume");
    return;
  }
};
struct Promise {
  int  _value;
  auto get_return_object() -> Generator {
    toy::debug("get_return_object");
    return Generator{ std::coroutine_handle<Promise>::from_promise(*this) };
  }
  auto initial_suspend() -> std::suspend_always {
    toy::debug("initial_suspend");
    return {};
  }
  auto final_suspend() noexcept -> std::suspend_never {
    toy::debug("final_suspend");
    return {};
  }
  void unhandled_exception() {}
  auto yield_value(int value) -> Awaiter {
    _value = value;
    return Awaiter{};
  }
};

int Generator::next() {
  toy::debug("before resume");
  resume();
  toy::debug("after resume");
  int value = promise()._value;
  return value;
}

Generator foo() {
  for (int i = 0;; i++) {
    toy::debugf("ready yield {}", i);
    co_yield i;
  }
}

// class Task;
// Task foo() {
//   co_return 1;
// }
// Task bar() {
//   int a = co_await foo();
// }


int main2() {
  toy::debug("call foo()");
  auto generator = foo();
  toy::debug(generator.next());
  toy::debug(generator.next());
  toy::debug(generator.next());
  toy::debug(generator.next());
  toy::debug(generator.next());
  toy::debug("call foo() done");
}