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
        return true;
    }

    void VulkanRenderBackend::Shutdown()
    {
        m_device.WaitIdle();
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

    void VulkanRenderBackend::OnResize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0) return;
        m_device.WaitIdle();
        m_swapchain.Recreate(m_device, width, height);
    }

} // namespace hybrid::renderer
