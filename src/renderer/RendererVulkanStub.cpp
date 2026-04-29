// Vulkan-mode stub for Renderer. Phase 0 of the Vulkan migration: this
// satisfies Renderer.h's API surface so the editor links, but does no
// rendering. The actual port lives behind the rhi/ interface in subsequent
// phases — see VULKAN_PLAN.md.
//
// In opengl mode this file is excluded from the build; the real
// src/renderer/Renderer.cpp is used instead.

#include "renderer/Renderer.h"
#include "renderer/raytracing/AccelerationStructureCache.h"

#include "core/Log.h"

namespace hybrid::renderer
{

    struct Renderer::Impl
    {
        bool initialized = false;
        RendererStats stats{};
        RendererOutputs outputs{};
        raytracing::AccelerationStructureStats as_stats{};
    };

    Renderer::Renderer() : m_impl(std::make_unique<Impl>()) {}
    Renderer::~Renderer() = default;

    bool Renderer::Init()
    {
        LOG_WARN("[renderer] Vulkan stub renderer active. The Vulkan backend is "
                 "currently in Phase 0 of the migration — see VULKAN_PLAN.md. "
                 "No frames will be rendered.");
        m_impl->initialized = true;
        return true;
    }

    void Renderer::Shutdown()
    {
        m_impl->initialized = false;
    }

    void Renderer::Resize(const RenderExtent & /*extent*/)
    {
        // no-op
    }

    bool Renderer::BeginFrame(const FrameContext & /*frame*/)
    {
        return m_impl->initialized;
    }

    void Renderer::SubmitScene(core::scene::SceneWorld & /*scene_world*/,
                                const RenderView & /*view*/,
                                const RenderSettings & /*settings*/)
    {
        // no-op
    }

    RendererOutputs Renderer::EndFrame()
    {
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
