import std;
import toy;

struct A {
  A(int a) {}
  A(const A&) noexcept = delete;
  A(A&&) noexcept = delete;
  auto operator=(const A&) noexcept -> A& = delete;
  auto operator=(A&&) noexcept -> A& = delete;
};

A func() { return { 1 }; }

int main() { auto a = new A{ func() }; }