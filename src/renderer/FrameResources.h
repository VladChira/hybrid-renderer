#pragma once

#include "renderer/RendererTypes.h"
#include "renderer/opengl/GLFramebuffer.h"
#include "renderer/opengl/GLTexture.h"

#include <algorithm>

namespace hybrid::renderer
{
    constexpr uint32_t kRaytraceShadowMaskLayerCount = 8;
    constexpr uint32_t kRaytraceEnvironmentShadowLayer = kRaytraceShadowMaskLayerCount - 1;

    enum class FrameTarget
    {
        SceneColor,
        SceneDepth,
        SceneColorRgb,
        SceneColorR,
        SceneColorG,
        SceneColorB,
        SceneColorA,
        GBufferRt0,
        GBufferRt1,
        GBufferRt0Rgb,
        GBufferRt0R,
        GBufferRt0G,
        GBufferRt0B,
        GBufferRt0A,
        GBufferRt1Rgb,
        GBufferRt1R,
        GBufferRt1G,
        GBufferRt1B,
        GBufferRt1A,
        GBufferEntityId,
        GBufferDepth,
        RaytraceHeatmap,
        RaytraceShadowMasks,
        RaytraceShadowHistoryA,
        RaytraceShadowHistoryB,
        RaytraceShadowAtrousPing,
        RaytraceShadowAtrousPong,
        // Full-res mask produced by joint-bilateral upscale of the half-res
        // shadow chain. Consumed by deferred lighting.
        RaytraceShadowMaskUpscaled
    };

    enum class FrameFramebuffer
    {
        Scene,
        GBuffer,
        DebugChannelExtract
    };

    class FrameResources
    {
    public:
        bool Resize(const RenderExtent &extent);
        void Reset();

        bool IsValid() const { return m_valid; }
        const RenderExtent &Extent() const { return m_extent; }
        // Extent used for the half-res shadow chain (mask, history, atrous).
        // The upscale pass produces a full-res mask from this. We round DOWN
        // so the integer ratio gbuffer_size / shadow_size is exactly 2 even
        // for odd render extents — the trace shader relies on this to map
        // half-res pixels onto full-res gbuffer pixels.
        static RenderExtent HalfResExtent(const RenderExtent &extent)
        {
            return RenderExtent{
                std::max<uint32_t>(1u, extent.width / 2u),
                std::max<uint32_t>(1u, extent.height / 2u)};
        }
        RenderExtent ShadowExtent() const { return HalfResExtent(m_extent); }

        GlTextureId Get(FrameTarget target) const;
        uint32_t GetFbo(FrameFramebuffer framebuffer) const;

    private:
        bool AllocateSceneTargets(const RenderExtent &extent);
        bool AllocateGBufferTargets(const RenderExtent &extent);
        bool AllocateDebugChannelTargets(const RenderExtent &extent);
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

        GLFramebuffer m_debug_channel_extract_framebuffer{};
        GLTexture m_scene_color_rgb{};
        GLTexture m_scene_color_r{};
        GLTexture m_scene_color_g{};
        GLTexture m_scene_color_b{};
        GLTexture m_scene_color_a{};
        GLTexture m_gbuffer_rt0_rgb{};
        GLTexture m_gbuffer_rt0_r{};
        GLTexture m_gbuffer_rt0_g{};
        GLTexture m_gbuffer_rt0_b{};
        GLTexture m_gbuffer_rt0_a{};
        GLTexture m_gbuffer_rt1_rgb{};
        GLTexture m_gbuffer_rt1_r{};
        GLTexture m_gbuffer_rt1_g{};
        GLTexture m_gbuffer_rt1_b{};
        GLTexture m_gbuffer_rt1_a{};

        GLTexture m_raytrace_heatmap{};
        GLTexture m_raytrace_shadow_masks{};
        GLTexture m_raytrace_shadow_history_a{};
        GLTexture m_raytrace_shadow_history_b{};
        GLTexture m_raytrace_shadow_atrous_ping{};
        GLTexture m_raytrace_shadow_atrous_pong{};
        GLTexture m_raytrace_shadow_mask_upscaled{};
    };

} // namespace hybrid::renderer
