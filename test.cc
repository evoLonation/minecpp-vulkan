#include <iostream>
#include <vector>

struct Test {
  enum Enum {
    A,
  };
  static auto a(Enum e) {}
};

int main() {
  auto a = std::vector<int>{};
  try {
    std::cout << a.front();
  } catch (std::exception) {
    std::cout << "error";
  }
}