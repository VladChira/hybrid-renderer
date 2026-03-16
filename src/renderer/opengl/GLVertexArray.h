#pragma once

#include <cstdint>

#include <glad.h>

namespace hybrid::renderer
{

    class GLVertexArray
    {
    public:
        GLVertexArray() = default;
        ~GLVertexArray();

        GLVertexArray(const GLVertexArray &) = delete;
        GLVertexArray &operator=(const GLVertexArray &) = delete;
        GLVertexArray(GLVertexArray &&other) noexcept;
        GLVertexArray &operator=(GLVertexArray &&other) noexcept;

        bool Create();
        void Destroy();

        void Bind() const;
        static void Unbind();

        void EnableAttrib(GLuint index) const;
        void DisableAttrib(GLuint index) const;
        void SetAttribPointer(GLuint index,
                              GLint size,
                              GLenum type,
                              bool normalized,
                              GLsizei stride,
                              uintptr_t offset) const;
        void SetAttribIPointer(GLuint index,
                               GLint size,
                               GLenum type,
                               GLsizei stride,
                               uintptr_t offset) const;
        void SetAttribDivisor(GLuint index, GLuint divisor) const;

        GLuint Id() const { return m_id; }
        bool IsValid() const { return m_id != 0; }

    private:
        GLuint m_id = 0;
    };

} // namespace hybrid::renderer
