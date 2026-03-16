#include "GLBuffer.h"

#include <utility>

namespace hybrid::renderer
{

    GLBuffer::GLBuffer(GLenum target)
    {
        Create(target);
    }

    GLBuffer::~GLBuffer()
    {
        Destroy();
    }

    GLBuffer::GLBuffer(GLBuffer &&other) noexcept
        : m_id(std::exchange(other.m_id, 0)),
          m_target(std::exchange(other.m_target, 0))
    {
    }

    GLBuffer &GLBuffer::operator=(GLBuffer &&other) noexcept
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

    bool GLBuffer::Create(GLenum target)
    {
        Destroy();
        glGenBuffers(1, &m_id);
        m_target = target;
        return m_id != 0;
    }

    void GLBuffer::Destroy()
    {
        if (m_id == 0)
        {
            return;
        }

        glDeleteBuffers(1, &m_id);
        m_id = 0;
        m_target = 0;
    }

    void GLBuffer::Bind() const
    {
        glBindBuffer(m_target, m_id);
    }

    void GLBuffer::Unbind(GLenum target)
    {
        glBindBuffer(target, 0);
    }

    void GLBuffer::SetData(GLsizeiptr size, const void *data, GLenum usage) const
    {
        glBufferData(m_target, size, data, usage);
    }

    void GLBuffer::SetSubData(GLintptr offset, GLsizeiptr size, const void *data) const
    {
        glBufferSubData(m_target, offset, size, data);
    }

    void *GLBuffer::MapRange(GLintptr offset, GLsizeiptr length, GLbitfield access) const
    {
        return glMapBufferRange(m_target, offset, length, access);
    }

    bool GLBuffer::Unmap() const
    {
        return glUnmapBuffer(m_target) == GL_TRUE;
    }

} // namespace hybrid::renderer
