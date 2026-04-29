// Single translation unit that defines VMA's implementation. Anywhere else
// that needs the allocator just includes <vk_mem_alloc.h> normally.
//
// We use the dynamic-function-loading mode so VMA resolves vk* function
// pointers via vkGetInstanceProcAddr / vkGetDeviceProcAddr at allocator
// creation time. This avoids link-time coupling to specific loader
// versions and matches how the rest of the engine calls Vulkan.

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
