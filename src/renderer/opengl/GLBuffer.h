#pragma once

#include <cstddef>
#include <cstdint>

#include <glad.h>

namespace hybrid::renderer
{

    class GLBuffer
    {
    public:
        GLBuffer() = default;
        explicit GLBuffer(GLenum target);
        ~GLBuffer();

        GLBuffer(const GLBuffer &) = delete;
        GLBuffer &operator=(const GLBuffer &) = delete;
        GLBuffer(GLBuffer &&other) noexcept;
        GLBuffer &operator=(GLBuffer &&other) noexcept;

        bool Create(GLenum target);
        void Destroy();

        void Bind() const;
        static void Unbind(GLenum target);

        void SetData(GLsizeiptr size, const void *data, GLenum usage) const;
        void SetSubData(GLintptr offset, GLsizeiptr size, const void *data) const;

        void *MapRange(GLintptr offset, GLsizeiptr length, GLbitfield access) const;
        bool Unmap() const;

        GLuint Id() const { return m_id; }
        GLenum Target() const { return m_target; }
        bool IsValid() const { return m_id != 0; }

    private:
        GLuint m_id = 0;
        GLenum m_target = 0;
    };

} // namespace hybrid::renderer
