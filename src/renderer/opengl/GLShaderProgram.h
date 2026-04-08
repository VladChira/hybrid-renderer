#pragma once

#include <string>
#include <string_view>

#include <glm/glm.hpp>
#include <glad.h>

namespace hybrid::renderer
{

    class GLShaderProgram
    {
    public:
        GLShaderProgram() = default;
        ~GLShaderProgram();

        GLShaderProgram(const GLShaderProgram &) = delete;
        GLShaderProgram &operator=(const GLShaderProgram &) = delete;
        GLShaderProgram(GLShaderProgram &&other) noexcept;
        GLShaderProgram &operator=(GLShaderProgram &&other) noexcept;

        bool LinkFromSource(std::string_view vertex_source,
                            std::string_view fragment_source,
                            std::string_view geometry_source = {});
        bool LinkComputeFromSource(std::string_view compute_source);
        void Destroy();

        void Use() const;
        static void Unuse();

        GLint GetUniformLocation(const char *name) const;
        void SetUniform1i(const char *name, GLint value) const;
        void SetUniform1ui(const char *name, GLuint value) const;
        void SetUniform1f(const char *name, GLfloat value) const;
        void SetUniformVec3(const char *name, const glm::vec3 &value) const;
        void SetUniformMat4(const char *name, const glm::mat4 &value) const;

        GLuint Id() const { return m_id; }
        bool IsValid() const { return m_id != 0; }

    private:
        static bool CompileShader(GLenum shader_type,
                                  std::string_view source,
                                  GLuint &out_shader,
                                  std::string &out_error_log);
        static std::string GetShaderInfoLog(GLuint shader);
        static std::string GetProgramInfoLog(GLuint program);

        GLuint m_id = 0;
    };

} // namespace hybrid::renderer
