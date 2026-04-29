// Vulkan-mode stub for Renderer. Phase 1: brings up the VulkanRenderBackend
// and clears the swapchain to a distinctive color each frame so we can
// confirm the Vulkan path is alive end-to-end. The "real" port (per-pass,
// behind the rhi/ interface) replaces this in subsequent phases — see
// VULKAN_PLAN.md.
//
// In opengl mode this file is excluded from the build; the real
// src/renderer/Renderer.cpp is used instead.

#include "renderer/Renderer.h"
#include "renderer/raytracing/AccelerationStructureCache.h"
#include "renderer/vulkan/VulkanRenderBackend.h"

#include "core/Log.h"

#include <GLFW/glfw3.h>

namespace hybrid::renderer
{

    namespace
    {
        // Distinctive teal so a black swapchain is obviously broken vs a
        // working clear.
        constexpr VkClearColorValue kClearColor = {{0.05f, 0.18f, 0.22f, 1.0f}};

        // Pipeline barrier: transition the swapchain image from layout `from`
        // to layout `to`, with the access masks the clear-color path needs.
        void TransitionSwapchainImage(VkCommandBuffer cmd,
                                       VkImage image,
                                       VkImageLayout from,
                                       VkImageLayout to,
                                       VkAccessFlags src_access,
                                       VkAccessFlags dst_access,
                                       VkPipelineStageFlags src_stage,
                                       VkPipelineStageFlags dst_stage)
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = from;
            b.newLayout = to;
            b.srcAccessMask = src_access;
            b.dstAccessMask = dst_access;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.baseMipLevel = 0;
            b.subresourceRange.levelCount = 1;
            b.subresourceRange.baseArrayLayer = 0;
            b.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                                 0, nullptr, 0, nullptr, 1, &b);
        }
    } // namespace

    struct Renderer::Impl
    {
        bool initialized = false;
        RendererStats stats{};
        RendererOutputs outputs{};
        raytracing::AccelerationStructureStats as_stats{};
        VulkanRenderBackend backend{};
        bool frame_active = false;
    };

    Renderer::Renderer() : m_impl(std::make_unique<Impl>()) {}
    Renderer::~Renderer() = default;

    bool Renderer::Init(platform::NativeWindowHandle window)
    {
        auto *glfw_window = static_cast<GLFWwindow *>(window.window);
        if (!glfw_window)
        {
            LOG_ERROR("[renderer/vulkan] Init missing window handle");
            return false;
        }

        int w = 0;
        int h = 0;
        glfwGetFramebufferSize(glfw_window, &w, &h);
        if (w <= 0 || h <= 0)
        {
            LOG_ERROR("[renderer/vulkan] window framebuffer size is zero");
            return false;
        }

        if (!m_impl->backend.Init(glfw_window,
                                   static_cast<uint32_t>(w),
                                   static_cast<uint32_t>(h)))
        {
            LOG_ERROR("[renderer/vulkan] backend Init failed");
            return false;
        }

        LOG_INFO("[renderer/vulkan] Phase 1 stub: clear-screen path active. "
                 "No scene rendering is hooked up yet.");
        m_impl->initialized = true;
        return true;
    }

    void Renderer::Shutdown()
    {
        if (!m_impl->initialized) return;
        m_impl->backend.Shutdown();
        m_impl->initialized = false;
    }

    void Renderer::Resize(const RenderExtent & /*extent*/)
    {
        // Swapchain recreation is driven by VK_ERROR_OUT_OF_DATE_KHR /
        // VK_SUBOPTIMAL_KHR returns from acquire/present, so we don't need
        // an explicit signal here. The window framebuffer size is fetched
        // from GLFW at recreate-time.
    }

    bool Renderer::BeginFrame(const FrameContext & /*frame*/)
    {
        if (!m_impl->initialized) return false;
        m_impl->frame_active = m_impl->backend.BeginFrame();
        return m_impl->frame_active;
    }

    void Renderer::SubmitScene(core::scene::SceneWorld & /*scene_world*/,
                                const RenderView & /*view*/,
                                const RenderSettings & /*settings*/)
    {
        // no-op until passes are ported
    }

    RendererOutputs Renderer::EndFrame()
    {
        if (!m_impl->frame_active)
        {
            return m_impl->outputs;
        }

        VkCommandBuffer cmd = m_impl->backend.CurrentCommandBuffer();
        VkImage image = m_impl->backend.CurrentSwapchainImage();

        // undefined -> TRANSFER_DST_OPTIMAL so we can clear it
        TransitionSwapchainImage(cmd, image,
                                 VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = 0;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = 1;
        vkCmdClearColorImage(cmd, image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &kClearColor, 1, &range);

        // TRANSFER_DST_OPTIMAL -> PRESENT_SRC_KHR for vkQueuePresent
        TransitionSwapchainImage(cmd, image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                 VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        m_impl->backend.EndFrame();
        m_impl->frame_active = false;
        return m_impl->outputs;
    }

    const RendererStats &Renderer::GetStats() const
    {
        return m_impl->stats;
    }

    const raytracing::AccelerationStructureStats *Renderer::GetAccelerationStructureStats() const
    {
        return &m_impl->as_stats;
    }

} // namespace hybrid::renderer
