#include "GLTexture.h"

#include <utility>

namespace hybrid::renderer
{

    GLTexture::GLTexture(GLenum target)
    {
        Create(target);
    }

    GLTexture::~GLTexture()
    {
        Destroy();
    }

    GLTexture::GLTexture(GLTexture &&other) noexcept
        : m_id(std::exchange(other.m_id, 0)),
          m_target(std::exchange(other.m_target, 0)),
          m_bindless_handle(std::exchange(other.m_bindless_handle, 0)),
          m_bindless_resident(std::exchange(other.m_bindless_resident, false))
    {
    }

    GLTexture &GLTexture::operator=(GLTexture &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        Destroy();
        m_id = std::exchange(other.m_id, 0);
        m_target = std::exchange(other.m_target, 0);
        m_bindless_handle = std::exchange(other.m_bindless_handle, 0);
        m_bindless_resident = std::exchange(other.m_bindless_resident, false);
        return *this;
    }

    bool GLTexture::Create(GLenum target)
    {
        Destroy();
        glGenTextures(1, &m_id);
        m_target = target;
        return m_id != 0;
    }

    void GLTexture::Destroy()
    {
        if (m_id == 0)
        {
            return;
        }

        MakeBindlessNonResident();
        m_bindless_handle = 0;

        glDeleteTextures(1, &m_id);
        m_id = 0;
        m_target = 0;
    }

    void GLTexture::Bind() const
    {
        glBindTexture(m_target, m_id);
    }

    void GLTexture::BindToUnit(uint32_t unit) const
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        Bind();
    }

    void GLTexture::Unbind(GLenum target)
    {
        glBindTexture(target, 0);
    }

    void GLTexture::SetParameter(GLenum parameter_name, GLint value) const
    {
        glTexParameteri(m_target, parameter_name, value);
    }

    void GLTexture::SetParameter(GLenum parameter_name, GLfloat value) const
    {
        glTexParameterf(m_target, parameter_name, value);
    }

    void GLTexture::SetImage2D(GLint level,
                               GLint internal_format,
                               GLsizei width,
                               GLsizei height,
                               GLenum format,
                               GLenum type,
                               const void *pixels) const
    {
        glTexImage2D(m_target, level, internal_format, width, height, 0, format, type, pixels);
    }

    void GLTexture::SetSubImage2D(GLint level,
                                  GLint xoffset,
                                  GLint yoffset,
                                  GLsizei width,
                                  GLsizei height,
                                  GLenum format,
                                  GLenum type,
                                  const void *pixels) const
    {
        glTexSubImage2D(m_target, level, xoffset, yoffset, width, height, format, type, pixels);
    }

    void GLTexture::GenerateMipmap() const
    {
        glGenerateMipmap(m_target);
    }

    bool GLTexture::IsBindlessTextureSupported()
    {
        return glGetTextureHandleARB != nullptr &&
               glMakeTextureHandleResidentARB != nullptr &&
               glMakeTextureHandleNonResidentARB != nullptr;
    }

    GLuint64 GLTexture::GetOrCreateBindlessHandle()
    {
        if (m_id == 0 || !IsBindlessTextureSupported())
        {
            return 0;
        }
        if (m_bindless_handle != 0)
        {
            return m_bindless_handle;
        }

        // Sampler state must be finalized before the handle is acquired — once
        // a texture handle exists, subsequent glTexParameter calls on the same
        // texture are ignored by the handle. Callers upload and configure
        // sampler params first, then request the handle.
        m_bindless_handle = glGetTextureHandleARB(m_id);
        return m_bindless_handle;
    }

    bool GLTexture::MakeBindlessResident()
    {
        if (m_bindless_resident)
        {
            return true;
        }
        if (GetOrCreateBindlessHandle() == 0)
        {
            return false;
        }
        glMakeTextureHandleResidentARB(m_bindless_handle);
        m_bindless_resident = true;
        return true;
    }

    void GLTexture::MakeBindlessNonResident()
    {
        if (!m_bindless_resident || m_bindless_handle == 0 || !IsBindlessTextureSupported())
        {
            m_bindless_resident = false;
            return;
        }
        glMakeTextureHandleNonResidentARB(m_bindless_handle);
        m_bindless_resident = false;
    }

} // namespace hybrid::renderer
