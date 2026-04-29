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
#ifdef HYBRID_DEBUG
        cfg.enable_validation = true;
#else
        cfg.enable_validation = false;
#endif
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

        vulkan::SwapchainConfig sc{};
        sc.surface = m_surface;
        sc.width = framebuffer_width;
        sc.height = framebuffer_height;
        if (!m_swapchain.Create(m_device, sc))
        {
            m_device.Destroy();
            m_instance.DestroySurface(m_surface);
            m_instance.Destroy();
            return false;
        }

        if (!CreateFrameData())
        {
            DestroyFrameData();
            m_swapchain.Destroy(m_device);
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
        DestroyFrameData();
        m_swapchain.Destroy(m_device);
        m_device.Destroy();
        if (m_surface != VK_NULL_HANDLE)
        {
            m_instance.DestroySurface(m_surface);
            m_surface = VK_NULL_HANDLE;
        }
        m_instance.Destroy();
        m_window = nullptr;
    }

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
            // Created signalled so the first frame's wait doesn't deadlock.
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

    bool VulkanRenderBackend::RecreateSwapchainFromWindow()
    {
        // GLFW reports zero size while minimised; just defer.
        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(m_window, &w, &h);
        if (w == 0 || h == 0) return false;

        m_device.WaitIdle();
        if (!m_swapchain.Recreate(m_device, static_cast<uint32_t>(w), static_cast<uint32_t>(h)))
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
        // Suboptimal is fine for this frame — we'll recreate after present.

        // Reset only after we know we'll submit (avoid resetting then aborting).
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
        // Wait at the broadest stage we'll write at. Transfer covers the
        // clear-image path; later, when raster passes target the swapchain,
        // bumping this to COLOR_ATTACHMENT_OUTPUT (or ALL_GRAPHICS) is
        // appropriate. Including both is safe and cheap.
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
