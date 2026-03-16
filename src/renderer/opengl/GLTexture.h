#pragma once

#include <cstdint>

#include <glad.h>

namespace hybrid::renderer
{

    class GLTexture
    {
    public:
        GLTexture() = default;
        explicit GLTexture(GLenum target);
        ~GLTexture();

        GLTexture(const GLTexture &) = delete;
        GLTexture &operator=(const GLTexture &) = delete;
        GLTexture(GLTexture &&other) noexcept;
        GLTexture &operator=(GLTexture &&other) noexcept;

        bool Create(GLenum target);
        void Destroy();

        void Bind() const;
        void BindToUnit(uint32_t unit) const;
        static void Unbind(GLenum target);

        void SetParameter(GLenum parameter_name, GLint value) const;
        void SetParameter(GLenum parameter_name, GLfloat value) const;

        void SetImage2D(GLint level,
                        GLint internal_format,
                        GLsizei width,
                        GLsizei height,
                        GLenum format,
                        GLenum type,
                        const void *pixels) const;
        void SetSubImage2D(GLint level,
                           GLint xoffset,
                           GLint yoffset,
                           GLsizei width,
                           GLsizei height,
                           GLenum format,
                           GLenum type,
                           const void *pixels) const;
        void GenerateMipmap() const;

        GLuint Id() const { return m_id; }
        GLenum Target() const { return m_target; }
        bool IsValid() const { return m_id != 0; }

    private:
        GLuint m_id = 0;
        GLenum m_target = 0;
    };

} // namespace hybrid::renderer
