#pragma once

#include <vector>

#include <glad.h>

namespace hybrid::renderer
{
    class GLTexture;

    class GLFramebuffer
    {
    public:
        GLFramebuffer() = default;
        GLFramebuffer(const GLFramebuffer &) = delete;
        GLFramebuffer &operator=(const GLFramebuffer &) = delete;
        GLFramebuffer(GLFramebuffer &&other) noexcept;
        GLFramebuffer &operator=(GLFramebuffer &&other) noexcept;
        ~GLFramebuffer();

        bool Create();
        void Destroy();

        void Bind(GLenum target = GL_FRAMEBUFFER) const;
        static void BindDefault(GLenum target = GL_FRAMEBUFFER);

        void AttachTexture2D(GLenum attachment,
                             const GLTexture &texture,
                             GLenum textarget = GL_TEXTURE_2D,
                             GLint level = 0) const;
        void AttachTextureLayer(GLenum attachment,
                                const GLTexture &texture,
                                GLint level,
                                GLint layer) const;

        void SetDrawBuffers(const std::vector<GLenum> &attachments) const;
        bool CheckComplete(GLenum target = GL_FRAMEBUFFER) const;

        GLuint Id() const { return m_id; }
        bool IsValid() const { return m_id != 0; }

    private:
        GLuint m_id = 0;
    };

} // namespace hybrid::renderer
