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
          m_target(std::exchange(other.m_target, 0))
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

} // namespace hybrid::renderer
