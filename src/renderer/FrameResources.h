#pragma once

#include "renderer/RendererTypes.h"
#include "renderer/opengl/GLFramebuffer.h"
#include "renderer/opengl/GLTexture.h"

#include <algorithm>

namespace hybrid::renderer
{

    enum class FrameTarget
    {
        SceneColor,
        SceneDepth,
        GBufferRt0,
        GBufferRt1,
        GBufferEntityId,
        GBufferDepth,
        RaytraceHeatmap,
        RaytraceAlbedo,
        RaytraceShadowMasks
    };

    // Maximum number of shadow-casting lights supported by the ray-traced
    // shadow mask array. Excess lights fall back to unshadowed.
    constexpr uint32_t kMaxShadowMaskLayers = 8;

    // Shadow masks are rendered at half resolution (both axes) and bilinearly
    // upsampled by the deferred lighting shader. 4× fewer rays per dispatch.
    constexpr uint32_t kShadowMaskResolutionDivisor = 2;

    inline RenderExtent ShadowMaskExtent(const RenderExtent &render_extent)
    {
        RenderExtent out{};
        out.width  = std::max<uint32_t>(1u, render_extent.width  / kShadowMaskResolutionDivisor);
        out.height = std::max<uint32_t>(1u, render_extent.height / kShadowMaskResolutionDivisor);
        return out;
    }

    enum class FrameFramebuffer
    {
        Scene,
        GBuffer
    };

    class FrameResources
    {
    public:
        bool Resize(const RenderExtent &extent);
        void Reset();

        bool IsValid() const { return m_valid; }
        const RenderExtent &Extent() const { return m_extent; }

        GlTextureId Get(FrameTarget target) const;
        uint32_t GetFbo(FrameFramebuffer framebuffer) const;

    private:
        bool AllocateSceneTargets(const RenderExtent &extent);
        bool AllocateGBufferTargets(const RenderExtent &extent);
        bool AllocateRaytraceTargets(const RenderExtent &extent);

        RenderExtent m_extent{};
        bool m_valid = false;

        GLFramebuffer m_scene_framebuffer{};
        GLTexture m_scene_color{};
        GLTexture m_scene_depth{};

        GLFramebuffer m_gbuffer_framebuffer{};
        GLTexture m_gbuffer_rt0{};
        GLTexture m_gbuffer_rt1{};
        GLTexture m_gbuffer_entity_id{};
        GLTexture m_gbuffer_depth{};

        GLTexture m_raytrace_heatmap{};
        GLTexture m_raytrace_albedo{};
        GLTexture m_raytrace_shadow_masks{};
    };

} // namespace hybrid::renderer
