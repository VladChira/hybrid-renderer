#include "GLFramebuffer.h"

#include "core/Log.h"
#include "GLTexture.h"

#include <utility>

namespace hybrid::renderer
{

    GLFramebuffer::GLFramebuffer(GLFramebuffer &&other) noexcept
        : m_id(std::exchange(other.m_id, 0))
    {
    }

    GLFramebuffer &GLFramebuffer::operator=(GLFramebuffer &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        Destroy();
        m_id = std::exchange(other.m_id, 0);
        return *this;
    }

    GLFramebuffer::~GLFramebuffer()
    {
        Destroy();
    }

    bool GLFramebuffer::Create()
    {
        Destroy();
        glGenFramebuffers(1, &m_id);
        return m_id != 0;
    }

    void GLFramebuffer::Destroy()
    {
        if (m_id == 0)
        {
            return;
        }

        glDeleteFramebuffers(1, &m_id);
        m_id = 0;
    }

    void GLFramebuffer::Bind(GLenum target) const
    {
        glBindFramebuffer(target, m_id);
    }

    void GLFramebuffer::BindDefault(GLenum target)
    {
        glBindFramebuffer(target, 0);
    }

    void GLFramebuffer::AttachTexture2D(GLenum attachment,
                                        const GLTexture &texture,
                                        GLenum textarget,
                                        GLint level) const
    {
        glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, textarget, texture.Id(), level);
    }

    void GLFramebuffer::AttachTextureLayer(GLenum attachment,
                                           const GLTexture &texture,
                                           GLint level,
                                           GLint layer) const
    {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, texture.Id(), level, layer);
    }

    void GLFramebuffer::SetDrawBuffers(const std::vector<GLenum> &attachments) const
    {
        glDrawBuffers(static_cast<GLsizei>(attachments.size()), attachments.data());
    }

    bool GLFramebuffer::CheckComplete(GLenum target) const
    {
        const GLenum status = glCheckFramebufferStatus(target);
        if (status == GL_FRAMEBUFFER_COMPLETE)
        {
            return true;
        }

        LOG_ERROR("[GLFramebuffer] Framebuffer incomplete. status={}", static_cast<unsigned>(status));
        return false;
    }

} // namespace hybrid::renderer
