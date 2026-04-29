#include "renderer/vulkan/VulkanRenderBackend.h"

#include <GLFW/glfw3.h>

namespace hybrid::renderer
{

    bool VulkanRenderBackend::Init(GLFWwindow *window,
                                    uint32_t framebuffer_width,
                                    uint32_t framebuffer_height)
    {
        m_window = window;

        vulkan::InstanceConfig cfg{};
        // Validation layers are cheap relative to the perf cost of
        // debugging Vulkan misuse without them. Keep on in all configs;
        // swap to a HYBRID_VK_VALIDATION compile flag if we want to flip
        // it off for release builds later.
        cfg.enable_validation = true;
#if defined(__APPLE__)
        cfg.require_macos_portability = true;
#else
        cfg.require_macos_portability = false;
#endif
        if (!m_instance.Create(cfg))
        {
            return false;
        }

        m_surface = m_instance.CreateSurface(window);
        if (m_surface == VK_NULL_HANDLE)
        {
            m_instance.Destroy();
            return false;
        }

        if (!m_device.Create(m_instance, m_surface))
        {
            m_instance.DestroySurface(m_surface);
            m_instance.Destroy();
            return false;
        }

        if (!CreateAllocator())
        {
            m_device.Destroy();
            m_instance.DestroySurface(m_surface);
            m_instance.Destroy();
            return false;
        }

        vulkan::SwapchainConfig sc{};
        sc.surface = m_surface;
        sc.width = framebuffer_width;
        sc.height = framebuffer_height;
        if (!m_swapchain.Create(m_device, sc))
        {
            DestroyAllocator();
            m_device.Destroy();
            m_instance.DestroySurface(m_surface);
            m_instance.Destroy();
            return false;
        }

        if (!CreateFrameData())
        {
            DestroyFrameData();
            m_swapchain.Destroy(m_device);
            DestroyAllocator();
            m_device.Destroy();
            m_instance.DestroySurface(m_surface);
            m_instance.Destroy();
            return false;
        }

        if (!CreateOffscreenSampler())
        {
            DestroyOffscreenSampler();
            DestroyFrameData();
            m_swapchain.Destroy(m_device);
            DestroyAllocator();
            m_device.Destroy();
            m_instance.DestroySurface(m_surface);
            m_instance.Destroy();
            return false;
        }

        if (!CreateOffscreenTarget(m_swapchain.Extent().width,
                                    m_swapchain.Extent().height))
        {
            DestroyOffscreenTarget();
            DestroyOffscreenSampler();
            DestroyFrameData();
            m_swapchain.Destroy(m_device);
            DestroyAllocator();
            m_device.Destroy();
            m_instance.DestroySurface(m_surface);
            m_instance.Destroy();
            return false;
        }

        if (!CreateDepthTarget(m_swapchain.Extent().width,
                                m_swapchain.Extent().height))
        {
            DestroyDepthTarget();
            DestroyOffscreenTarget();
            DestroyOffscreenSampler();
            DestroyFrameData();
            m_swapchain.Destroy(m_device);
            DestroyAllocator();
            m_device.Destroy();
            m_instance.DestroySurface(m_surface);
            m_instance.Destroy();
            return false;
        }

        return true;
    }

    void VulkanRenderBackend::Shutdown()
    {
        m_device.WaitIdle();
        DestroyDepthTarget();
        DestroyOffscreenTarget();
        DestroyOffscreenSampler();
        DestroyFrameData();
        m_swapchain.Destroy(m_device);
        DestroyAllocator();
        m_device.Destroy();
        if (m_surface != VK_NULL_HANDLE)
        {
            m_instance.DestroySurface(m_surface);
            m_surface = VK_NULL_HANDLE;
        }
        m_instance.Destroy();
        m_window = nullptr;
    }

    // -----------------------------------------------------------------------
    // VMA
    // -----------------------------------------------------------------------

    bool VulkanRenderBackend::CreateAllocator()
    {
        VmaVulkanFunctions vfns{};
        vfns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vfns.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo info{};
        info.physicalDevice = m_device.Physical();
        info.device         = m_device.Logical();
        info.instance       = m_instance.Handle();
        info.vulkanApiVersion = VK_API_VERSION_1_3;
        info.pVulkanFunctions = &vfns;

        return HYBRID_VK_CHECK(vmaCreateAllocator(&info, &m_allocator));
    }

    void VulkanRenderBackend::DestroyAllocator()
    {
        if (m_allocator != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(m_allocator);
            m_allocator = VK_NULL_HANDLE;
        }
    }

    // -----------------------------------------------------------------------
    // Per-frame command pool / sync
    // -----------------------------------------------------------------------

    bool VulkanRenderBackend::CreateFrameData()
    {
        for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
        {
            FrameData &f = m_frames[i];

            VkCommandPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            pool_info.queueFamilyIndex = *m_device.Queues().graphics;
            if (!HYBRID_VK_CHECK(vkCreateCommandPool(m_device.Logical(), &pool_info, nullptr, &f.command_pool)))
            {
                return false;
            }

            VkCommandBufferAllocateInfo cb_info{};
            cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cb_info.commandPool = f.command_pool;
            cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cb_info.commandBufferCount = 1;
            if (!HYBRID_VK_CHECK(vkAllocateCommandBuffers(m_device.Logical(), &cb_info, &f.command_buffer)))
            {
                return false;
            }

            VkSemaphoreCreateInfo sem_info{};
            sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            if (!HYBRID_VK_CHECK(vkCreateSemaphore(m_device.Logical(), &sem_info, nullptr, &f.image_available)) ||
                !HYBRID_VK_CHECK(vkCreateSemaphore(m_device.Logical(), &sem_info, nullptr, &f.render_finished)))
            {
                return false;
            }

            VkFenceCreateInfo fence_info{};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (!HYBRID_VK_CHECK(vkCreateFence(m_device.Logical(), &fence_info, nullptr, &f.in_flight)))
            {
                return false;
            }
        }
        return true;
    }

    void VulkanRenderBackend::DestroyFrameData()
    {
        for (FrameData &f : m_frames)
        {
            if (f.in_flight)        { vkDestroyFence(m_device.Logical(), f.in_flight, nullptr);          f.in_flight = VK_NULL_HANDLE; }
            if (f.render_finished)  { vkDestroySemaphore(m_device.Logical(), f.render_finished, nullptr); f.render_finished = VK_NULL_HANDLE; }
            if (f.image_available)  { vkDestroySemaphore(m_device.Logical(), f.image_available, nullptr); f.image_available = VK_NULL_HANDLE; }
            if (f.command_pool)     { vkDestroyCommandPool(m_device.Logical(), f.command_pool, nullptr);  f.command_pool = VK_NULL_HANDLE; f.command_buffer = VK_NULL_HANDLE; }
        }
    }

    // -----------------------------------------------------------------------
    // Offscreen target
    // -----------------------------------------------------------------------

    bool VulkanRenderBackend::CreateOffscreenTarget(uint32_t width, uint32_t height)
    {
        m_offscreen.format = VK_FORMAT_R8G8B8A8_UNORM;
        m_offscreen.extent = {width, height};

        VkImageCreateInfo img_info{};
        img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType     = VK_IMAGE_TYPE_2D;
        img_info.format        = m_offscreen.format;
        img_info.extent.width  = width;
        img_info.extent.height = height;
        img_info.extent.depth  = 1;
        img_info.mipLevels     = 1;
        img_info.arrayLayers   = 1;
        img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
        img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        img_info.usage         = VK_IMAGE_USAGE_STORAGE_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT |
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

        if (!HYBRID_VK_CHECK(vmaCreateImage(m_allocator, &img_info, &alloc_info,
                                             &m_offscreen.image,
                                             &m_offscreen.allocation,
                                             nullptr)))
        {
            return false;
        }

        VkImageViewCreateInfo view_info{};
        view_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image    = m_offscreen.image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format   = m_offscreen.format;
        view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel   = 0;
        view_info.subresourceRange.levelCount     = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount     = 1;

        return HYBRID_VK_CHECK(vkCreateImageView(m_device.Logical(), &view_info, nullptr, &m_offscreen.view));
    }

    void VulkanRenderBackend::DestroyOffscreenTarget()
    {
        if (m_offscreen.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_device.Logical(), m_offscreen.view, nullptr);
            m_offscreen.view = VK_NULL_HANDLE;
        }
        if (m_offscreen.image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(m_allocator, m_offscreen.image, m_offscreen.allocation);
            m_offscreen.image = VK_NULL_HANDLE;
            m_offscreen.allocation = VK_NULL_HANDLE;
        }
        m_offscreen.extent = {0, 0};
    }

    bool VulkanRenderBackend::CreateDepthTarget(uint32_t width, uint32_t height)
    {
        m_depth.format = VK_FORMAT_D32_SFLOAT;
        m_depth.extent = {width, height};

        VkImageCreateInfo img_info{};
        img_info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        img_info.imageType     = VK_IMAGE_TYPE_2D;
        img_info.format        = m_depth.format;
        img_info.extent.width  = width;
        img_info.extent.height = height;
        img_info.extent.depth  = 1;
        img_info.mipLevels     = 1;
        img_info.arrayLayers   = 1;
        img_info.samples       = VK_SAMPLE_COUNT_1_BIT;
        img_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        img_info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        img_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_AUTO;

        if (!HYBRID_VK_CHECK(vmaCreateImage(m_allocator, &img_info, &alloc_info,
                                             &m_depth.image,
                                             &m_depth.allocation,
                                             nullptr)))
        {
            return false;
        }

        VkImageViewCreateInfo view_info{};
        view_info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image    = m_depth.image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format   = m_depth.format;
        view_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        view_info.subresourceRange.levelCount     = 1;
        view_info.subresourceRange.layerCount     = 1;

        return HYBRID_VK_CHECK(vkCreateImageView(m_device.Logical(), &view_info, nullptr, &m_depth.view));
    }

    void VulkanRenderBackend::DestroyDepthTarget()
    {
        if (m_depth.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_device.Logical(), m_depth.view, nullptr);
            m_depth.view = VK_NULL_HANDLE;
        }
        if (m_depth.image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(m_allocator, m_depth.image, m_depth.allocation);
            m_depth.image = VK_NULL_HANDLE;
            m_depth.allocation = VK_NULL_HANDLE;
        }
        m_depth.extent = {0, 0};
    }

    bool VulkanRenderBackend::CreateOffscreenSampler()
    {
        VkSamplerCreateInfo s{};
        s.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        s.magFilter    = VK_FILTER_LINEAR;
        s.minFilter    = VK_FILTER_LINEAR;
        s.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        s.minLod       = 0.0f;
        s.maxLod       = 0.0f;
        return HYBRID_VK_CHECK(vkCreateSampler(m_device.Logical(), &s, nullptr, &m_offscreen_sampler));
    }

    void VulkanRenderBackend::DestroyOffscreenSampler()
    {
        if (m_offscreen_sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_device.Logical(), m_offscreen_sampler, nullptr);
            m_offscreen_sampler = VK_NULL_HANDLE;
        }
    }

    // -----------------------------------------------------------------------
    // Frame loop
    // -----------------------------------------------------------------------

    bool VulkanRenderBackend::RecreateSwapchainFromWindow()
    {
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(m_window, &w, &h);
        if (w == 0 || h == 0) return false;

        m_device.WaitIdle();
        if (!m_swapchain.Recreate(m_device, static_cast<uint32_t>(w), static_cast<uint32_t>(h)))
        {
            return false;
        }
        DestroyOffscreenTarget();
        DestroyDepthTarget();
        if (!CreateOffscreenTarget(static_cast<uint32_t>(w), static_cast<uint32_t>(h)) ||
            !CreateDepthTarget(static_cast<uint32_t>(w), static_cast<uint32_t>(h)))
        {
            return false;
        }
        m_swapchain_invalid = false;
        return true;
    }

    bool VulkanRenderBackend::BeginFrame()
    {
        if (m_swapchain_invalid)
        {
            if (!RecreateSwapchainFromWindow()) return false;
        }

        FrameData &f = m_frames[m_frame_index];

        vkWaitForFences(m_device.Logical(), 1, &f.in_flight, VK_TRUE, UINT64_MAX);

        VkResult acquire = vkAcquireNextImageKHR(
            m_device.Logical(), m_swapchain.Handle(), UINT64_MAX,
            f.image_available, VK_NULL_HANDLE, &m_image_index);

        if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_swapchain_invalid = true;
            return false;
        }
        if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
        {
            HYBRID_VK_CHECK(acquire);
            return false;
        }

        vkResetFences(m_device.Logical(), 1, &f.in_flight);
        vkResetCommandBuffer(f.command_buffer, 0);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!HYBRID_VK_CHECK(vkBeginCommandBuffer(f.command_buffer, &begin_info)))
        {
            return false;
        }

        m_current_command_buffer = f.command_buffer;
        return true;
    }

    void VulkanRenderBackend::EndFrame()
    {
        if (m_current_command_buffer == VK_NULL_HANDLE) return;

        FrameData &f = m_frames[m_frame_index];

        if (!HYBRID_VK_CHECK(vkEndCommandBuffer(f.command_buffer)))
        {
            return;
        }

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkPipelineStageFlags wait_stage =
            VK_PIPELINE_STAGE_TRANSFER_BIT |
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &f.image_available;
        submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &f.command_buffer;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &f.render_finished;
        HYBRID_VK_CHECK(vkQueueSubmit(m_device.GraphicsQueue(), 1, &submit, f.in_flight));

        VkSwapchainKHR swapchain = m_swapchain.Handle();
        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &f.render_finished;
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &m_image_index;

        VkResult present_result = vkQueuePresentKHR(m_device.PresentQueue(), &present);
        if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
            present_result == VK_SUBOPTIMAL_KHR)
        {
            m_swapchain_invalid = true;
        }
        else
        {
            HYBRID_VK_CHECK(present_result);
        }

        m_current_command_buffer = VK_NULL_HANDLE;
        m_frame_index = (m_frame_index + 1) % kMaxFramesInFlight;
    }

    VkImage VulkanRenderBackend::CurrentSwapchainImage() const
    {
        return m_swapchain.Image(m_image_index);
    }

    VkImageView VulkanRenderBackend::CurrentSwapchainImageView() const
    {
        return m_swapchain.ImageView(m_image_index);
    }

} // namespace hybrid::renderer
