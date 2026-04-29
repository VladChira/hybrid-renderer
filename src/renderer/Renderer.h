#pragma once

#include "renderer/RendererTypes.h"
#include "core/scene/SceneWorld.h"
#include "platform/PlatformEvents.h"

#include <memory>

#ifdef HYBRID_RHI_VULKAN
#include <vulkan/vulkan.h>
#include <functional>
#endif

namespace hybrid::renderer::raytracing
{
    struct AccelerationStructureStats;
}

namespace hybrid::renderer
{
#ifdef HYBRID_RHI_VULKAN
    class VulkanRenderBackend;
#endif

    class Renderer
    {
    public:
        struct Impl;

        Renderer();
        ~Renderer();

        Renderer(const Renderer &) = delete;
        Renderer &operator=(const Renderer &) = delete;
        Renderer(Renderer &&) noexcept = default;
        Renderer &operator=(Renderer &&) noexcept = default;

        // The native window is used by the Vulkan backend to create its
        // surface + swapchain. The OpenGL backend ignores it (the GL context
        // is bound to the current thread by the platform layer).
        bool Init(platform::NativeWindowHandle window = {});
        void Shutdown();

        void Resize(const RenderExtent &extent);

        bool BeginFrame(const FrameContext &frame);
        void SubmitScene(core::scene::SceneWorld &scene_world,
                         const RenderView &view,
                         const RenderSettings &settings);
        RendererOutputs EndFrame();

        const RendererStats &GetStats() const;

        // Pointer to the live acceleration-structure stats block owned by the
        // renderer. Valid for the renderer's lifetime.
        const raytracing::AccelerationStructureStats *GetAccelerationStructureStats() const;

#ifdef HYBRID_RHI_VULKAN
        // Hook called by EndFrame between the heatmap blit and present, with
        // the swapchain image already in COLOR_ATTACHMENT_OPTIMAL and a
        // dynamic-rendering scope open. Used by the Ui module to record
        // ImGui draws into the renderer's command buffer. Set once at
        // startup; null hook = no UI rendering.
        using UiRenderHook = std::function<void(VkCommandBuffer)>;
        void SetUiRenderHook(UiRenderHook hook);

        // Backend accessor for the UI module to pull Vulkan handles
        // (instance/device/queue/format/etc.) it needs at Init time.
        VulkanRenderBackend *GetVulkanRenderBackend();
#endif

    private:
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer
