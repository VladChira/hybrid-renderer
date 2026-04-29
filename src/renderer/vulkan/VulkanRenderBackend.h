#pragma once

#include "renderer/vulkan/VulkanCommon.h"

#include "renderer/vulkan/VulkanInstance.h"
#include "renderer/vulkan/VulkanDevice.h"
#include "renderer/vulkan/VulkanSwapchain.h"

#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>

struct GLFWwindow;

namespace hybrid::renderer
{

    // The Vulkan render backend ties a window to an instance/device/swapchain
    // and owns the per-frame command pool + sync primitives so the rest of
    // the renderer can do acquire / record / submit / present each frame.
    //
    // It also owns:
    //   * VMA allocator (used by callers to create images / buffers)
    //   * an offscreen color image at swapchain extent (the "scratch" target
    //     that compute passes write into; blitted to the swapchain at end of
    //     frame). This will eventually become a proper FrameResources-style
    //     pool but is fine inline while the only consumer is the stub
    //     gradient pass.
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

        // ---- accessors during a frame -----------------------------------
        VkCommandBuffer CurrentCommandBuffer() const { return m_current_command_buffer; }
        VkImage  CurrentSwapchainImage() const;
        VkImageView CurrentSwapchainImageView() const;
        VkExtent2D SwapchainExtent() const { return m_swapchain.Extent(); }
        VkFormat   SwapchainFormat() const { return m_swapchain.ColorFormat(); }
        uint32_t   FrameIndexInFlight() const { return m_frame_index; }

        // ---- offscreen target -------------------------------------------
        // STORAGE + SAMPLED + COLOR_ATTACHMENT RGBA8 image, sized to
        // swapchain extent. Recreated alongside the swapchain on resize.
        // ImageView is bound as a storage image by the heatmap compute pass,
        // as a color attachment by the gbuffer raster pass, and as a
        // combined-image-sampler by ImGui's ViewportPanel. The shared
        // sampler is owned by the backend for the lifetime of the device.
        VkImage     OffscreenImage()      const { return m_offscreen.image; }
        VkImageView OffscreenImageView()  const { return m_offscreen.view; }
        VkFormat    OffscreenFormat()     const { return m_offscreen.format; }
        VkExtent2D  OffscreenExtent()     const { return m_offscreen.extent; }
        VkSampler   OffscreenSampler()    const { return m_offscreen_sampler; }

        // ---- depth target -----------------------------------------------
        // D32_SFLOAT depth image at the same extent. Used by the gbuffer
        // raster pass for per-pixel depth testing. Recreated on resize.
        VkImage     DepthImage()       const { return m_depth.image; }
        VkImageView DepthImageView()   const { return m_depth.view; }
        VkFormat    DepthFormat()      const { return m_depth.format; }

        // ---- sub-objects ------------------------------------------------
        vulkan::Instance &Instance()           { return m_instance; }
        vulkan::Device &Device()               { return m_device; }
        vulkan::Swapchain &Swapchain()         { return m_swapchain; }
        VmaAllocator Allocator() const         { return m_allocator; }

    private:
        struct FrameData
        {
            VkCommandPool command_pool      = VK_NULL_HANDLE;
            VkCommandBuffer command_buffer  = VK_NULL_HANDLE;
            VkSemaphore image_available     = VK_NULL_HANDLE;
            VkSemaphore render_finished     = VK_NULL_HANDLE;
            VkFence in_flight               = VK_NULL_HANDLE;
        };

        struct OffscreenTarget
        {
            VkImage image           = VK_NULL_HANDLE;
            VkImageView view        = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            VkFormat format         = VK_FORMAT_R8G8B8A8_UNORM;
            VkExtent2D extent       = {0, 0};
        };

        struct DepthTarget
        {
            VkImage image           = VK_NULL_HANDLE;
            VkImageView view        = VK_NULL_HANDLE;
            VmaAllocation allocation = VK_NULL_HANDLE;
            VkFormat format         = VK_FORMAT_D32_SFLOAT;
            VkExtent2D extent       = {0, 0};
        };

        bool CreateAllocator();
        void DestroyAllocator();
        bool CreateFrameData();
        void DestroyFrameData();
        bool CreateOffscreenTarget(uint32_t width, uint32_t height);
        void DestroyOffscreenTarget();
        bool CreateDepthTarget(uint32_t width, uint32_t height);
        void DestroyDepthTarget();
        bool CreateOffscreenSampler();
        void DestroyOffscreenSampler();
        bool RecreateSwapchainFromWindow();

        GLFWwindow *m_window = nullptr;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        vulkan::Instance m_instance{};
        vulkan::Device m_device{};
        vulkan::Swapchain m_swapchain{};
        VmaAllocator m_allocator = VK_NULL_HANDLE;

        std::array<FrameData, kMaxFramesInFlight> m_frames{};
        uint32_t m_frame_index = 0;            // index into m_frames
        uint32_t m_image_index = 0;            // current swapchain image
        bool m_swapchain_invalid = false;
        VkCommandBuffer m_current_command_buffer = VK_NULL_HANDLE;

        OffscreenTarget m_offscreen{};
        DepthTarget m_depth{};
        VkSampler m_offscreen_sampler = VK_NULL_HANDLE;
    };

} // namespace hybrid::renderer
