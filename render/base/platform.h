#ifdef PLATFORM_MACOS
#define PORTABILITY_SUBSET true
#else
#define PORTABILITY_SUBSET false
#endif

#ifdef PLATFORM_MACOS
#define PLATFORM_VULKAN_VERSION VK_API_VERSION_1_2
#else
#define PLATFORM_VULKAN_VERSION VK_API_VERSION_1_3
#endif

namespace rd::platform {
constexpr bool portability_subset = PORTABILITY_SUBSET;
}