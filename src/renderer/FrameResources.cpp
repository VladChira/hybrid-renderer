#include "renderer/FrameResources.h"

#include "core/Log.h"

namespace hybrid::renderer
{

    bool FrameResources::Resize(const RenderExtent &extent)
    {
        if (m_valid &&
            m_extent.width == extent.width &&
            m_extent.height == extent.height)
        {
            return true;
        }

        m_extent = extent;
        if (!m_extent.IsValid())
        {
            m_valid = false;
            return false;
        }

        if (!AllocateSceneTargets(m_extent))
        {
            m_valid = false;
            return false;
        }

        if (!AllocateGBufferTargets(m_extent))
        {
            m_valid = false;
            return false;
        }

        if (!AllocateDebugChannelTargets(m_extent))
        {
            m_valid = false;
            return false;
        }

        if (!AllocateRaytraceTargets(m_extent))
        {
            m_valid = false;
            return false;
        }


        m_valid = true;
        return true;
    }

    void FrameResources::Reset()
    {
        m_scene_color.Destroy();
        m_scene_depth.Destroy();
        m_scene_framebuffer.Destroy();
        m_gbuffer_rt0.Destroy();
        m_gbuffer_rt1.Destroy();
        m_gbuffer_entity_id.Destroy();
        m_gbuffer_depth.Destroy();
        m_gbuffer_framebuffer.Destroy();
        m_scene_color_rgb.Destroy();
        m_scene_color_r.Destroy();
        m_scene_color_g.Destroy();
        m_scene_color_b.Destroy();
        m_scene_color_a.Destroy();
        m_gbuffer_rt0_rgb.Destroy();
        m_gbuffer_rt0_r.Destroy();
        m_gbuffer_rt0_g.Destroy();
        m_gbuffer_rt0_b.Destroy();
        m_gbuffer_rt0_a.Destroy();
        m_gbuffer_rt1_rgb.Destroy();
        m_gbuffer_rt1_r.Destroy();
        m_gbuffer_rt1_g.Destroy();
        m_gbuffer_rt1_b.Destroy();
        m_gbuffer_rt1_a.Destroy();
        m_raytrace_heatmap.Destroy();
        m_raytrace_shadow_masks.Destroy();
        m_raytrace_shadow_history_a.Destroy();
        m_raytrace_shadow_history_b.Destroy();
        m_raytrace_shadow_atrous_ping.Destroy();
        m_raytrace_shadow_atrous_pong.Destroy();
        m_prev_gbuffer_depth.Destroy();
        m_prev_gbuffer_rt1.Destroy();
        m_raytrace_reflection_radiance.Destroy();
        m_raytrace_reflection_history_a.Destroy();
        m_raytrace_reflection_history_b.Destroy();
        m_raytrace_reflection_atrous_ping.Destroy();
        m_raytrace_reflection_atrous_pong.Destroy();
        m_debug_channel_extract_framebuffer.Destroy();
        m_extent = {};
        m_valid = false;
    }

    GlTextureId FrameResources::Get(FrameTarget target) const
    {
        switch (target)
        {
        case FrameTarget::SceneColor:
            return m_scene_color.Id();
        case FrameTarget::SceneDepth:
            return m_scene_depth.Id();
        case FrameTarget::SceneColorRgb:
            return m_scene_color_rgb.Id();
        case FrameTarget::SceneColorR:
            return m_scene_color_r.Id();
        case FrameTarget::SceneColorG:
            return m_scene_color_g.Id();
        case FrameTarget::SceneColorB:
            return m_scene_color_b.Id();
        case FrameTarget::SceneColorA:
            return m_scene_color_a.Id();
        case FrameTarget::GBufferRt0:
            return m_gbuffer_rt0.Id();
        case FrameTarget::GBufferRt1:
            return m_gbuffer_rt1.Id();
        case FrameTarget::GBufferRt0Rgb:
            return m_gbuffer_rt0_rgb.Id();
        case FrameTarget::GBufferRt0R:
            return m_gbuffer_rt0_r.Id();
        case FrameTarget::GBufferRt0G:
            return m_gbuffer_rt0_g.Id();
        case FrameTarget::GBufferRt0B:
            return m_gbuffer_rt0_b.Id();
        case FrameTarget::GBufferRt0A:
            return m_gbuffer_rt0_a.Id();
        case FrameTarget::GBufferRt1Rgb:
            return m_gbuffer_rt1_rgb.Id();
        case FrameTarget::GBufferRt1R:
            return m_gbuffer_rt1_r.Id();
        case FrameTarget::GBufferRt1G:
            return m_gbuffer_rt1_g.Id();
        case FrameTarget::GBufferRt1B:
            return m_gbuffer_rt1_b.Id();
        case FrameTarget::GBufferRt1A:
            return m_gbuffer_rt1_a.Id();
        case FrameTarget::GBufferEntityId:
            return m_gbuffer_entity_id.Id();
        case FrameTarget::GBufferDepth:
            return m_gbuffer_depth.Id();
        case FrameTarget::RaytraceHeatmap:
            return m_raytrace_heatmap.Id();
        case FrameTarget::RaytraceShadowMasks:
            return m_raytrace_shadow_masks.Id();
        case FrameTarget::RaytraceShadowHistoryA:
            return m_raytrace_shadow_history_a.Id();
        case FrameTarget::RaytraceShadowHistoryB:
            return m_raytrace_shadow_history_b.Id();
        case FrameTarget::RaytraceShadowAtrousPing:
            return m_raytrace_shadow_atrous_ping.Id();
        case FrameTarget::RaytraceShadowAtrousPong:
            return m_raytrace_shadow_atrous_pong.Id();
        case FrameTarget::PrevGBufferDepth:
            return m_prev_gbuffer_depth.Id();
        case FrameTarget::PrevGBufferRt1:
            return m_prev_gbuffer_rt1.Id();
        case FrameTarget::RaytraceReflectionRadiance:
            return m_raytrace_reflection_radiance.Id();
        case FrameTarget::RaytraceReflectionHistoryA:
            return m_raytrace_reflection_history_a.Id();
        case FrameTarget::RaytraceReflectionHistoryB:
            return m_raytrace_reflection_history_b.Id();
        case FrameTarget::RaytraceReflectionAtrousPing:
            return m_raytrace_reflection_atrous_ping.Id();
        case FrameTarget::RaytraceReflectionAtrousPong:
            return m_raytrace_reflection_atrous_pong.Id();
        default:
            return 0;
        }
    }

    uint32_t FrameResources::GetFbo(FrameFramebuffer framebuffer) const
    {
        switch (framebuffer)
        {
        case FrameFramebuffer::Scene:
            return m_scene_framebuffer.Id();
        case FrameFramebuffer::GBuffer:
            return m_gbuffer_framebuffer.Id();
        case FrameFramebuffer::DebugChannelExtract:
            return m_debug_channel_extract_framebuffer.Id();
        default:
            return 0;
        }
    }

    bool FrameResources::AllocateSceneTargets(const RenderExtent &extent)
    {
        if (!extent.IsValid())
        {
            return false;
        }

        if (!m_scene_framebuffer.IsValid() && !m_scene_framebuffer.Create())
        {
            LOG_ERROR("[FrameResources] Failed to create scene framebuffer");
            return false;
        }

        if (!m_scene_color.IsValid() && !m_scene_color.Create(GL_TEXTURE_2D))
        {
            LOG_ERROR("[FrameResources] Failed to create scene color texture");
            return false;
        }

        if (!m_scene_depth.IsValid() && !m_scene_depth.Create(GL_TEXTURE_2D))
        {
            LOG_ERROR("[FrameResources] Failed to create scene depth texture");
            return false;
        }

        m_scene_color.Bind();
        m_scene_color.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_scene_color.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_scene_color.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_scene_color.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_scene_color.SetImage2D(0,
                                 GL_RGBA8,
                                 static_cast<GLsizei>(extent.width),
                                 static_cast<GLsizei>(extent.height),
                                 GL_RGBA,
                                 GL_UNSIGNED_BYTE,
                                 nullptr);

        m_scene_depth.Bind();
        m_scene_depth.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_scene_depth.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_scene_depth.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_scene_depth.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_scene_depth.SetImage2D(0,
                                 GL_DEPTH_COMPONENT24,
                                 static_cast<GLsizei>(extent.width),
                                 static_cast<GLsizei>(extent.height),
                                 GL_DEPTH_COMPONENT,
                                 GL_FLOAT,
                                 nullptr);

        m_scene_framebuffer.Bind(GL_FRAMEBUFFER);
        m_scene_framebuffer.AttachTexture2D(GL_COLOR_ATTACHMENT0, m_scene_color);
        m_scene_framebuffer.AttachTexture2D(GL_DEPTH_ATTACHMENT, m_scene_depth);
        m_scene_framebuffer.SetDrawBuffers({GL_COLOR_ATTACHMENT0});
        const bool complete = m_scene_framebuffer.CheckComplete(GL_FRAMEBUFFER);
        GLFramebuffer::BindDefault(GL_FRAMEBUFFER);

        if (!complete)
        {
            LOG_ERROR("[FrameResources] Scene framebuffer is incomplete");
            return false;
        }

        return true;
    }

    bool FrameResources::AllocateGBufferTargets(const RenderExtent &extent)
    {
        if (!extent.IsValid())
        {
            return false;
        }

        if (!m_gbuffer_framebuffer.IsValid() && !m_gbuffer_framebuffer.Create())
        {
            LOG_ERROR("[FrameResources] Failed to create gbuffer framebuffer");
            return false;
        }

        if (!m_gbuffer_rt0.IsValid() && !m_gbuffer_rt0.Create(GL_TEXTURE_2D))
        {
            LOG_ERROR("[FrameResources] Failed to create gbuffer rt0 texture");
            return false;
        }

        if (!m_gbuffer_rt1.IsValid() && !m_gbuffer_rt1.Create(GL_TEXTURE_2D))
        {
            LOG_ERROR("[FrameResources] Failed to create gbuffer rt1 texture");
            return false;
        }

        if (!m_gbuffer_depth.IsValid() && !m_gbuffer_depth.Create(GL_TEXTURE_2D))
        {
            LOG_ERROR("[FrameResources] Failed to create gbuffer depth texture");
            return false;
        }

        if (!m_gbuffer_entity_id.IsValid() && !m_gbuffer_entity_id.Create(GL_TEXTURE_2D))
        {
            LOG_ERROR("[FrameResources] Failed to create gbuffer entity id texture");
            return false;
        }

        m_gbuffer_rt0.Bind();
        m_gbuffer_rt0.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_gbuffer_rt0.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_gbuffer_rt0.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gbuffer_rt0.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gbuffer_rt0.SetImage2D(0,
                                 GL_RGBA8,
                                 static_cast<GLsizei>(extent.width),
                                 static_cast<GLsizei>(extent.height),
                                 GL_RGBA,
                                 GL_UNSIGNED_BYTE,
                                 nullptr);

        m_gbuffer_rt1.Bind();
        m_gbuffer_rt1.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_gbuffer_rt1.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_gbuffer_rt1.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gbuffer_rt1.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gbuffer_rt1.SetImage2D(0,
                                 GL_RGBA16F,
                                 static_cast<GLsizei>(extent.width),
                                 static_cast<GLsizei>(extent.height),
                                 GL_RGBA,
                                 GL_HALF_FLOAT,
                                 nullptr);

        m_gbuffer_entity_id.Bind();
        m_gbuffer_entity_id.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_gbuffer_entity_id.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_gbuffer_entity_id.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gbuffer_entity_id.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gbuffer_entity_id.SetImage2D(0,
                                       GL_R32UI,
                                       static_cast<GLsizei>(extent.width),
                                       static_cast<GLsizei>(extent.height),
                                       GL_RED_INTEGER,
                                       GL_UNSIGNED_INT,
                                       nullptr);

        m_gbuffer_depth.Bind();
        m_gbuffer_depth.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_gbuffer_depth.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_gbuffer_depth.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_gbuffer_depth.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_gbuffer_depth.SetImage2D(0,
                                   GL_DEPTH_COMPONENT24,
                                   static_cast<GLsizei>(extent.width),
                                   static_cast<GLsizei>(extent.height),
                                   GL_DEPTH_COMPONENT,
                                   GL_FLOAT,
                                   nullptr);

        m_gbuffer_framebuffer.Bind(GL_FRAMEBUFFER);
        m_gbuffer_framebuffer.AttachTexture2D(GL_COLOR_ATTACHMENT0, m_gbuffer_rt0);
        m_gbuffer_framebuffer.AttachTexture2D(GL_COLOR_ATTACHMENT1, m_gbuffer_rt1);
        m_gbuffer_framebuffer.AttachTexture2D(GL_COLOR_ATTACHMENT2, m_gbuffer_entity_id);
        m_gbuffer_framebuffer.AttachTexture2D(GL_DEPTH_ATTACHMENT, m_gbuffer_depth);
        m_gbuffer_framebuffer.SetDrawBuffers({GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2});
        const bool complete = m_gbuffer_framebuffer.CheckComplete(GL_FRAMEBUFFER);
        GLFramebuffer::BindDefault(GL_FRAMEBUFFER);

        if (!complete)
        {
            LOG_ERROR("[FrameResources] GBuffer framebuffer is incomplete");
            return false;
        }

        return true;
    }

    bool FrameResources::AllocateDebugChannelTargets(const RenderExtent &extent)
    {
        if (!extent.IsValid())
        {
            return false;
        }

        if (!m_debug_channel_extract_framebuffer.IsValid() && !m_debug_channel_extract_framebuffer.Create())
        {
            LOG_ERROR("[FrameResources] Failed to create debug channel extract framebuffer");
            return false;
        }

        auto allocate_debug_texture = [extent](GLTexture &texture, const char *label) -> bool
        {
            if (!texture.IsValid() && !texture.Create(GL_TEXTURE_2D))
            {
                LOG_ERROR("[FrameResources] Failed to create {}", label);
                return false;
            }

            texture.Bind();
            texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            texture.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            texture.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            texture.SetImage2D(0,
                               GL_RGBA8,
                               static_cast<GLsizei>(extent.width),
                               static_cast<GLsizei>(extent.height),
                               GL_RGBA,
                               GL_UNSIGNED_BYTE,
                               nullptr);
            return true;
        };

        if (!allocate_debug_texture(m_scene_color_rgb, "scene color RGB preview texture") ||
            !allocate_debug_texture(m_scene_color_r, "scene color R channel texture") ||
            !allocate_debug_texture(m_scene_color_g, "scene color G channel texture") ||
            !allocate_debug_texture(m_scene_color_b, "scene color B channel texture") ||
            !allocate_debug_texture(m_scene_color_a, "scene color A channel texture") ||
            !allocate_debug_texture(m_gbuffer_rt0_rgb, "gbuffer rt0 RGB preview texture") ||
            !allocate_debug_texture(m_gbuffer_rt0_r, "gbuffer rt0 R channel texture") ||
            !allocate_debug_texture(m_gbuffer_rt0_g, "gbuffer rt0 G channel texture") ||
            !allocate_debug_texture(m_gbuffer_rt0_b, "gbuffer rt0 B channel texture") ||
            !allocate_debug_texture(m_gbuffer_rt0_a, "gbuffer rt0 A channel texture") ||
            !allocate_debug_texture(m_gbuffer_rt1_rgb, "gbuffer rt1 RGB preview texture") ||
            !allocate_debug_texture(m_gbuffer_rt1_r, "gbuffer rt1 R channel texture") ||
            !allocate_debug_texture(m_gbuffer_rt1_g, "gbuffer rt1 G channel texture") ||
            !allocate_debug_texture(m_gbuffer_rt1_b, "gbuffer rt1 B channel texture") ||
            !allocate_debug_texture(m_gbuffer_rt1_a, "gbuffer rt1 A channel texture"))
        {
            return false;
        }

        m_debug_channel_extract_framebuffer.Bind(GL_FRAMEBUFFER);
        m_debug_channel_extract_framebuffer.AttachTexture2D(GL_COLOR_ATTACHMENT0, m_scene_color_r);
        m_debug_channel_extract_framebuffer.SetDrawBuffers({GL_COLOR_ATTACHMENT0});
        const bool complete = m_debug_channel_extract_framebuffer.CheckComplete(GL_FRAMEBUFFER);
        GLFramebuffer::BindDefault(GL_FRAMEBUFFER);

        if (!complete)
        {
            LOG_ERROR("[FrameResources] Debug channel extract framebuffer is incomplete");
            return false;
        }

        return true;
    }

    bool FrameResources::AllocateRaytraceTargets(const RenderExtent &extent)
    {
        if (!extent.IsValid())
        {
            return false;
        }

        if (!m_raytrace_heatmap.IsValid() && !m_raytrace_heatmap.Create(GL_TEXTURE_2D))
        {
            LOG_ERROR("[FrameResources] Failed to create raytrace heatmap texture");
            return false;
        }

        m_raytrace_heatmap.Bind();
        m_raytrace_heatmap.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_raytrace_heatmap.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_raytrace_heatmap.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_raytrace_heatmap.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_raytrace_heatmap.SetImage2D(0,
                                      GL_RGBA8,
                                      static_cast<GLsizei>(extent.width),
                                      static_cast<GLsizei>(extent.height),
                                      GL_RGBA,
                                      GL_UNSIGNED_BYTE,
                                      nullptr);

        if (!m_raytrace_shadow_masks.IsValid() && !m_raytrace_shadow_masks.Create(GL_TEXTURE_2D_ARRAY))
        {
            LOG_ERROR("[FrameResources] Failed to create raytrace shadow mask array");
            return false;
        }
        m_raytrace_shadow_masks.Bind();
        m_raytrace_shadow_masks.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_raytrace_shadow_masks.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_raytrace_shadow_masks.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_raytrace_shadow_masks.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_raytrace_shadow_masks.SetImage3D(0,
                                           GL_R8,
                                           static_cast<GLsizei>(extent.width),
                                           static_cast<GLsizei>(extent.height),
                                           static_cast<GLsizei>(kRaytraceShadowMaskLayerCount),
                                           GL_RED,
                                           GL_UNSIGNED_BYTE,
                                           nullptr);

        auto allocate_r16f_array = [extent](GLTexture &texture, const char *label) -> bool
        {
            if (!texture.IsValid() && !texture.Create(GL_TEXTURE_2D_ARRAY))
            {
                LOG_ERROR("[FrameResources] Failed to create {}", label);
                return false;
            }
            texture.Bind();
            texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            texture.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            texture.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            texture.SetImage3D(0,
                               GL_R16F,
                               static_cast<GLsizei>(extent.width),
                               static_cast<GLsizei>(extent.height),
                               static_cast<GLsizei>(kRaytraceShadowMaskLayerCount),
                               GL_RED,
                               GL_HALF_FLOAT,
                               nullptr);
            return true;
        };

        if (!allocate_r16f_array(m_raytrace_shadow_history_a, "raytrace shadow history A array") ||
            !allocate_r16f_array(m_raytrace_shadow_history_b, "raytrace shadow history B array") ||
            !allocate_r16f_array(m_raytrace_shadow_atrous_ping, "raytrace shadow atrous ping array") ||
            !allocate_r16f_array(m_raytrace_shadow_atrous_pong, "raytrace shadow atrous pong array"))
        {
            return false;
        }

        // Previous-frame gbuffer copies. Same internal formats as the live
        // gbuffer so glCopyImageSubData can copy without conversion.
        if (!m_prev_gbuffer_depth.IsValid() && !m_prev_gbuffer_depth.Create(GL_TEXTURE_2D))
        {
            LOG_ERROR("[FrameResources] Failed to create prev gbuffer depth texture");
            return false;
        }
        m_prev_gbuffer_depth.Bind();
        m_prev_gbuffer_depth.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_prev_gbuffer_depth.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_prev_gbuffer_depth.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_prev_gbuffer_depth.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_prev_gbuffer_depth.SetImage2D(0,
                                        GL_DEPTH_COMPONENT24,
                                        static_cast<GLsizei>(extent.width),
                                        static_cast<GLsizei>(extent.height),
                                        GL_DEPTH_COMPONENT,
                                        GL_FLOAT,
                                        nullptr);

        if (!m_prev_gbuffer_rt1.IsValid() && !m_prev_gbuffer_rt1.Create(GL_TEXTURE_2D))
        {
            LOG_ERROR("[FrameResources] Failed to create prev gbuffer rt1 texture");
            return false;
        }
        m_prev_gbuffer_rt1.Bind();
        m_prev_gbuffer_rt1.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        m_prev_gbuffer_rt1.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        m_prev_gbuffer_rt1.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        m_prev_gbuffer_rt1.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_prev_gbuffer_rt1.SetImage2D(0,
                                      GL_RGBA16F,
                                      static_cast<GLsizei>(extent.width),
                                      static_cast<GLsizei>(extent.height),
                                      GL_RGBA,
                                      GL_HALF_FLOAT,
                                      nullptr);

        // Reflection radiance and denoising temporaries — all rgba16f, 2D (not arrayed).
        auto allocate_rgba16f = [extent](GLTexture &texture, const char *label) -> bool
        {
            if (!texture.IsValid() && !texture.Create(GL_TEXTURE_2D))
            {
                LOG_ERROR("[FrameResources] Failed to create {}", label);
                return false;
            }
            texture.Bind();
            texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            texture.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            texture.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            texture.SetImage2D(0,
                               GL_RGBA16F,
                               static_cast<GLsizei>(extent.width),
                               static_cast<GLsizei>(extent.height),
                               GL_RGBA,
                               GL_HALF_FLOAT,
                               nullptr);
            return true;
        };

        if (!allocate_rgba16f(m_raytrace_reflection_radiance,  "raytrace reflection radiance texture")  ||
            !allocate_rgba16f(m_raytrace_reflection_history_a, "raytrace reflection history A texture") ||
            !allocate_rgba16f(m_raytrace_reflection_history_b, "raytrace reflection history B texture") ||
            !allocate_rgba16f(m_raytrace_reflection_atrous_ping, "raytrace reflection atrous ping texture") ||
            !allocate_rgba16f(m_raytrace_reflection_atrous_pong, "raytrace reflection atrous pong texture"))
        {
            return false;
        }

        return true;
    }


} // namespace hybrid::renderer
