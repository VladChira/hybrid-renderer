#include "GLVertexArray.h"

#include <utility>

namespace hybrid::renderer
{

    GLVertexArray::~GLVertexArray()
    {
        Destroy();
    }

    GLVertexArray::GLVertexArray(GLVertexArray &&other) noexcept
        : m_id(std::exchange(other.m_id, 0))
    {
    }

    GLVertexArray &GLVertexArray::operator=(GLVertexArray &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        Destroy();
        m_id = std::exchange(other.m_id, 0);
        return *this;
    }

    bool GLVertexArray::Create()
    {
        Destroy();
        glGenVertexArrays(1, &m_id);
        return m_id != 0;
    }

    void GLVertexArray::Destroy()
    {
        if (m_id == 0)
        {
            return;
        }

        glDeleteVertexArrays(1, &m_id);
        m_id = 0;
    }

    void GLVertexArray::Bind() const
    {
        glBindVertexArray(m_id);
    }

    void GLVertexArray::Unbind()
    {
        glBindVertexArray(0);
    }

    void GLVertexArray::EnableAttrib(GLuint index) const
    {
        glEnableVertexAttribArray(index);
    }

    void GLVertexArray::DisableAttrib(GLuint index) const
    {
        glDisableVertexAttribArray(index);
    }

    void GLVertexArray::SetAttribPointer(GLuint index,
                                         GLint size,
                                         GLenum type,
                                         bool normalized,
                                         GLsizei stride,
                                         uintptr_t offset) const
    {
        glVertexAttribPointer(index,
                              size,
                              type,
                              normalized ? GL_TRUE : GL_FALSE,
                              stride,
                              reinterpret_cast<const void *>(offset));
    }

    void GLVertexArray::SetAttribIPointer(GLuint index,
                                          GLint size,
                                          GLenum type,
                                          GLsizei stride,
                                          uintptr_t offset) const
    {
        glVertexAttribIPointer(index, size, type, stride, reinterpret_cast<const void *>(offset));
    }

    void GLVertexArray::SetAttribDivisor(GLuint index, GLuint divisor) const
    {
        glVertexAttribDivisor(index, divisor);
    }

} // namespace hybrid::renderer
