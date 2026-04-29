#pragma once

#include "renderer/vulkan/VulkanCommon.h"

#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanSwapchain.h"

struct GLFWwindow;

namespace hybrid::renderer
{

    // The Vulkan render backend ties a window to an instance/device/swapchain
    // and (eventually) implements the rhi::Device interface for the rest of
    // the renderer to consume. Today it only stands up the device.
    class VulkanRenderBackend
    {
    public:
        bool Init(GLFWwindow *window, uint32_t framebuffer_width, uint32_t framebuffer_height);
        void Shutdown();

        void OnResize(uint32_t width, uint32_t height);

        vulkan::Instance &Instance()           { return m_instance; }
        vulkan::Device &Device()               { return m_device; }
        vulkan::Swapchain &Swapchain()         { return m_swapchain; }

    private:
        GLFWwindow *m_window = nullptr;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        vulkan::Instance m_instance{};
        vulkan::Device m_device{};
        vulkan::Swapchain m_swapchain{};
    };

} // namespace hybrid::renderer
