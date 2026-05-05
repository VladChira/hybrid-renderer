#pragma once

#include "renderer/RendererTypes.h"
#include "renderer/opengl/GLFramebuffer.h"
#include "renderer/opengl/GLTexture.h"

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
        RaytracePrimaryAlbedo,
        RaytraceReflectionsRaw,
        RaytraceReflections,
        RaytraceHeatmap,
        RaytraceShadowMasks,
        RaytraceShadowHistoryA,
        RaytraceShadowHistoryB,
        RaytraceShadowAtrousPing,
        RaytraceShadowAtrousPong,
        RaytraceReflectionHistoryA,
        RaytraceReflectionHistoryB,
        RaytraceReflectionAtrousPing,
        RaytraceReflectionAtrousPong,
        // Copies of the previous frame's gbuffer used by the temporal
        // accumulation pass for disocclusion-aware history rejection.
        PrevGBufferDepth,
        PrevGBufferRt1
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

        GLTexture m_raytrace_primary_albedo{};
        GLTexture m_raytrace_reflections_raw{};
        GLTexture m_raytrace_reflections{};
        GLTexture m_raytrace_heatmap{};
        GLTexture m_raytrace_shadow_masks{};
        GLTexture m_raytrace_shadow_history_a{};
        GLTexture m_raytrace_shadow_history_b{};
        GLTexture m_raytrace_shadow_atrous_ping{};
        GLTexture m_raytrace_shadow_atrous_pong{};
        GLTexture m_raytrace_reflection_history_a{};
        GLTexture m_raytrace_reflection_history_b{};
        GLTexture m_raytrace_reflection_atrous_ping{};
        GLTexture m_raytrace_reflection_atrous_pong{};
        GLTexture m_prev_gbuffer_depth{};
        GLTexture m_prev_gbuffer_rt1{};
    };

} // namespace hybrid::renderer
