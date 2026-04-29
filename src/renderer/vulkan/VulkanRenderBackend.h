#pragma once

#include "renderer/vulkan/VulkanCommon.h"

#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanSwapchain.h"

#include <array>
#include <cstdint>

struct GLFWwindow;

namespace hybrid::renderer
{

    // The Vulkan render backend ties a window to an instance/device/swapchain
    // and owns the per-frame command pool + sync primitives so the rest of
    // the renderer can do acquire / record / submit / present each frame.
    class VulkanRenderBackend
    {
    public:
        // Two frames in flight. See VULKAN_PLAN.md §4.3.
        static constexpr uint32_t kMaxFramesInFlight = 2;

        bool Init(GLFWwindow *window, uint32_t framebuffer_width, uint32_t framebuffer_height);
        void Shutdown();

        // Acquire the next swapchain image and start a fresh command buffer.
        // Returns false when the swapchain became out-of-date (recreated
        // automatically — caller should skip rendering this frame).
        bool BeginFrame();

        // End command-buffer recording, submit, present.
        void EndFrame();

        // Currently-recording command buffer (valid between BeginFrame and
        // EndFrame). Returns VK_NULL_HANDLE when not in a frame.
        VkCommandBuffer CurrentCommandBuffer() const { return m_current_command_buffer; }

        // The swapchain image acquired this frame (valid between BeginFrame
        // and EndFrame).
        VkImage CurrentSwapchainImage() const;
        VkImageView CurrentSwapchainImageView() const;
        VkExtent2D SwapchainExtent() const { return m_swapchain.Extent(); }
        VkFormat SwapchainFormat() const { return m_swapchain.ColorFormat(); }

        vulkan::Instance &Instance()           { return m_instance; }
        vulkan::Device &Device()               { return m_device; }
        vulkan::Swapchain &Swapchain()         { return m_swapchain; }

    private:
        struct FrameData
        {
            VkCommandPool command_pool      = VK_NULL_HANDLE;
            VkCommandBuffer command_buffer  = VK_NULL_HANDLE;
            VkSemaphore image_available     = VK_NULL_HANDLE;
            VkSemaphore render_finished     = VK_NULL_HANDLE;
            VkFence in_flight               = VK_NULL_HANDLE;
        };

        bool CreateFrameData();
        void DestroyFrameData();
        bool RecreateSwapchainFromWindow();

        GLFWwindow *m_window = nullptr;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        vulkan::Instance m_instance{};
        vulkan::Device m_device{};
        vulkan::Swapchain m_swapchain{};

        std::array<FrameData, kMaxFramesInFlight> m_frames{};
        uint32_t m_frame_index = 0;            // index into m_frames
        uint32_t m_image_index = 0;            // current swapchain image
        bool m_swapchain_invalid = false;

        // Convenience accessors during a frame.
        VkCommandBuffer m_current_command_buffer = VK_NULL_HANDLE;
    };

} // namespace hybrid::renderer
