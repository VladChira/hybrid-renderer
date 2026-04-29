#include "renderer/vulkan/VulkanBuffer.h"

namespace hybrid::renderer::vulkan
{

    Buffer CreateHostVisibleBuffer(VmaAllocator allocator,
                                   VkDeviceSize size,
                                   VkBufferUsageFlags usage)
    {
        Buffer out{};
        if (size == 0)
        {
            LOG_ERROR("[vulkan/buffer] CreateHostVisibleBuffer with size=0");
            return out;
        }

        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = size;
        bi.usage       = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_AUTO;
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo alloc_info{};
        if (!HYBRID_VK_CHECK(vmaCreateBuffer(allocator, &bi, &ai,
                                              &out.handle,
                                              &out.allocation,
                                              &alloc_info)))
        {
            return Buffer{};
        }
        out.size   = size;
        out.mapped = alloc_info.pMappedData;
        return out;
    }

    void DestroyBuffer(VmaAllocator allocator, Buffer &buffer)
    {
        if (buffer.handle != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(allocator, buffer.handle, buffer.allocation);
        }
        buffer = Buffer{};
    }

} // namespace hybrid::renderer::vulkan
