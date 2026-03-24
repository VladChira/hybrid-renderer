#include "renderer/FrameResources.h"

#include "core/Log.h"

namespace hybrid::renderer
{

    bool FrameResources::Resize(const RenderExtent &extent)
    {
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
        case FrameTarget::GBufferRt0:
            return m_gbuffer_rt0.Id();
        case FrameTarget::GBufferRt1:
            return m_gbuffer_rt1.Id();
        case FrameTarget::GBufferEntityId:
            return m_gbuffer_entity_id.Id();
        case FrameTarget::GBufferDepth:
            return m_gbuffer_depth.Id();
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

} // namespace hybrid::renderer
