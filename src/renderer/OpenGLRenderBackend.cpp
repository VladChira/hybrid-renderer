#include "renderer/OpenGLRenderBackend.h"

#include "renderer/opengl/GLFramebuffer.h"

namespace hybrid::renderer
{

    bool OpenGLRenderBackend::BeginFrame(uint32_t scene_framebuffer_id, const RenderExtent &extent) const
    {
        if (scene_framebuffer_id == 0 || !extent.IsValid())
        {
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, scene_framebuffer_id);
        glViewport(0, 0, static_cast<GLsizei>(extent.width), static_cast<GLsizei>(extent.height));
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return true;
    }

    void OpenGLRenderBackend::EndFrame() const
    {
        GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
    }

} // namespace hybrid::renderer
