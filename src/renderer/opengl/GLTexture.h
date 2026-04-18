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
        void SetImage3D(GLint level,
                        GLint internal_format,
                        GLsizei width,
                        GLsizei height,
                        GLsizei depth,
                        GLenum format,
                        GLenum type,
                        const void *pixels) const;
        void GenerateMipmap() const;

        // Bindless texture helpers (GL_ARB_bindless_texture). Returns 0 if the
        // extension is unavailable or the handle could not be acquired.
        GLuint64 GetOrCreateBindlessHandle();
        bool MakeBindlessResident();
        void MakeBindlessNonResident();
        GLuint64 BindlessHandle() const { return m_bindless_handle; }
        bool IsBindlessResident() const { return m_bindless_resident; }

        GLuint Id() const { return m_id; }
        GLenum Target() const { return m_target; }
        bool IsValid() const { return m_id != 0; }

        static bool IsBindlessTextureSupported();

    private:
        GLuint m_id = 0;
        GLenum m_target = 0;
        GLuint64 m_bindless_handle = 0;
        bool m_bindless_resident = false;
    };

} // namespace hybrid::renderer
