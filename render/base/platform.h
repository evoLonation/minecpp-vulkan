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

namespace platform {

#ifdef PLATFORM_WINDOWS
constexpr bool windows = true;
#else
constexpr bool windows = false;
#endif
#ifdef PLATFORM_MACOS
constexpr bool macos = true;
#else
constexpr bool macos = false;
#endif


constexpr bool portability_subset = PORTABILITY_SUBSET;

} // namespace rd::platform