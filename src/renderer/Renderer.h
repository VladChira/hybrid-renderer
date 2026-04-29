#pragma once

#include "renderer/RendererTypes.h"
#include "core/scene/SceneWorld.h"
#include "platform/PlatformEvents.h"

#include <memory>

namespace hybrid::renderer::raytracing
{
    struct AccelerationStructureStats;
}

namespace hybrid::renderer
{
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

    private:
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer
