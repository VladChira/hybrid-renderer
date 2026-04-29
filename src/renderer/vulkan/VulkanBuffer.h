#pragma once

#include "renderer/vulkan/VulkanCommon.h"

#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdint>

namespace hybrid::renderer::vulkan
{

    // Lightweight VMA buffer wrapper. `mapped` is non-null when the buffer was
    // allocated as host-visible + mapped — write to it directly.
    struct Buffer
    {
        VkBuffer       handle     = VK_NULL_HANDLE;
        VmaAllocation  allocation = VK_NULL_HANDLE;
        VkDeviceSize   size       = 0;
        void          *mapped     = nullptr;
    };

    // Host-visible, sequential-write, persistently mapped. Right shape for
    // small SSBOs and per-frame UBOs driven from the CPU. Returns a Buffer
    // with `handle == VK_NULL_HANDLE` on failure.
    Buffer CreateHostVisibleBuffer(VmaAllocator allocator,
                                   VkDeviceSize size,
                                   VkBufferUsageFlags usage);

    void DestroyBuffer(VmaAllocator allocator, Buffer &buffer);

} // namespace hybrid::renderer::vulkan
