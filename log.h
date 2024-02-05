#ifndef LOG_H
#define LOG_H

#include <array>
#include <ranges>
#include <format>
#include <print>
#include <stdexcept>

constexpr bool enableDebugOutput = true;

template<size_t number>
class BracesString {
private:
	static consteval auto getFormatStringArray() {
		std::array<char, number*4> arr{};
		for(auto i: std::ranges::iota_view(static_cast<size_t>(0), number-1)) {
			arr[i*4] = '{';
			arr[i*4+1] = '}';
			arr[i*4+2] = ',';
			arr[i*4+3] = ' ';
		}
		arr[(number-1)*4] = '{';
		arr[(number-1)*4+1] = '}';
		arr[(number-1)*4+2] = ' ';
		arr[(number-1)*4+3] = '\0';
		return arr;
	}
	static constexpr std::array<char, number*4> value = getFormatStringArray();
public:
	static consteval const char* get() {
		return value.data();
	}
};

template <typename... Args>
void debugf(std::format_string<Args...> fmt, Args &&...args) {
  if constexpr (enableDebugOutput) {
    std::println(fmt, std::forward<Args>(args)...);
  }
}
template <typename... Args>
auto check_debugf(bool condition, std::format_string<Args...> fmt,
                  Args &&...args) -> bool {
  if (!condition) {
    debugf(fmt, std::forward<Args>(args)...);
  }
  return condition;
}

template <typename... Args> void debug(Args &&...args) {
  if constexpr (enableDebugOutput) {
    std::println(
        std::format_string<Args...>(BracesString<sizeof...(Args)>::get()),
        std::forward<Args>(args)...);
  }
}

template <typename... Args>
void throwf(std::format_string<Args...> fmt, Args &&...args) {
  throw std::runtime_error{std::format(fmt, std::forward<Args>(args)...)};
}

template <typename... Args>
void check_throwf(bool condition, std::format_string<Args...> fmt,
                  Args &&...args) {
  if (!condition) {
    throwf(fmt, std::forward<Args>(args)...);
  }
}


#endif // LOG_H
