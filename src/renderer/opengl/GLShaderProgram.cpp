#include "GLShaderProgram.h"

#include "core/Log.h"

#include <glm/gtc/type_ptr.hpp>

#include <utility>
#include <vector>

namespace hybrid::renderer
{

    GLShaderProgram::~GLShaderProgram()
    {
        Destroy();
    }

    GLShaderProgram::GLShaderProgram(GLShaderProgram &&other) noexcept
        : m_id(std::exchange(other.m_id, 0))
    {
    }

    GLShaderProgram &GLShaderProgram::operator=(GLShaderProgram &&other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        Destroy();
        m_id = std::exchange(other.m_id, 0);
        return *this;
    }

    bool GLShaderProgram::LinkFromSource(std::string_view vertex_source,
                                         std::string_view fragment_source,
                                         std::string_view geometry_source)
    {
        Destroy();

        GLuint vertex_shader = 0;
        GLuint fragment_shader = 0;
        GLuint geometry_shader = 0;
        std::string error_log;

        if (!CompileShader(GL_VERTEX_SHADER, vertex_source, vertex_shader, error_log))
        {
            LOG_ERROR("[GLShaderProgram] Vertex shader compilation failed:\n{}", error_log);
            return false;
        }

        if (!CompileShader(GL_FRAGMENT_SHADER, fragment_source, fragment_shader, error_log))
        {
            glDeleteShader(vertex_shader);
            LOG_ERROR("[GLShaderProgram] Fragment shader compilation failed:\n{}", error_log);
            return false;
        }

        if (!geometry_source.empty() &&
            !CompileShader(GL_GEOMETRY_SHADER, geometry_source, geometry_shader, error_log))
        {
            glDeleteShader(vertex_shader);
            glDeleteShader(fragment_shader);
            LOG_ERROR("[GLShaderProgram] Geometry shader compilation failed:\n{}", error_log);
            return false;
        }

        m_id = glCreateProgram();
        if (m_id == 0)
        {
            glDeleteShader(vertex_shader);
            glDeleteShader(fragment_shader);
            if (geometry_shader != 0)
            {
                glDeleteShader(geometry_shader);
            }
            LOG_ERROR("[GLShaderProgram] glCreateProgram failed");
            return false;
        }

        glAttachShader(m_id, vertex_shader);
        glAttachShader(m_id, fragment_shader);
        if (geometry_shader != 0)
        {
            glAttachShader(m_id, geometry_shader);
        }

        glLinkProgram(m_id);
        GLint link_success = GL_FALSE;
        glGetProgramiv(m_id, GL_LINK_STATUS, &link_success);

        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        if (geometry_shader != 0)
        {
            glDeleteShader(geometry_shader);
        }

        if (link_success != GL_TRUE)
        {
            const std::string info_log = GetProgramInfoLog(m_id);
            LOG_ERROR("[GLShaderProgram] Program link failed:\n{}", info_log);
            Destroy();
            return false;
        }

        return true;
    }

    bool GLShaderProgram::LinkComputeFromSource(std::string_view compute_source)
    {
        Destroy();

        GLuint compute_shader = 0;
        std::string error_log;

        if (!CompileShader(GL_COMPUTE_SHADER, compute_source, compute_shader, error_log))
        {
            LOG_ERROR("[GLShaderProgram] Compute shader compilation failed:\n{}", error_log);
            return false;
        }

        m_id = glCreateProgram();
        if (m_id == 0)
        {
            glDeleteShader(compute_shader);
            LOG_ERROR("[GLShaderProgram] glCreateProgram failed");
            return false;
        }

        glAttachShader(m_id, compute_shader);
        glLinkProgram(m_id);

        GLint link_success = GL_FALSE;
        glGetProgramiv(m_id, GL_LINK_STATUS, &link_success);
        glDeleteShader(compute_shader);

        if (link_success != GL_TRUE)
        {
            const std::string info_log = GetProgramInfoLog(m_id);
            LOG_ERROR("[GLShaderProgram] Compute program link failed:\n{}", info_log);
            Destroy();
            return false;
        }

        return true;
    }

    void GLShaderProgram::Destroy()
    {
        m_uniform_location_cache.clear();

        if (m_id == 0)
        {
            return;
        }

        glDeleteProgram(m_id);
        m_id = 0;
    }

    void GLShaderProgram::Use() const
    {
        glUseProgram(m_id);
    }

    void GLShaderProgram::Unuse()
    {
        glUseProgram(0);
    }

    GLint GLShaderProgram::GetUniformLocation(const char *name) const
    {
        if (name == nullptr || m_id == 0)
        {
            return -1;
        }

        if (const auto it = m_uniform_location_cache.find(name); it != m_uniform_location_cache.end())
        {
            return it->second;
        }

        const GLint location = glGetUniformLocation(m_id, name);
        m_uniform_location_cache.emplace(name, location);
        return location;
    }

    void GLShaderProgram::SetUniform1i(const char *name, GLint value) const
    {
        glUniform1i(GetUniformLocation(name), value);
    }

    void GLShaderProgram::SetUniform1ui(const char *name, GLuint value) const
    {
        glUniform1ui(GetUniformLocation(name), value);
    }

    void GLShaderProgram::SetUniform1f(const char *name, GLfloat value) const
    {
        glUniform1f(GetUniformLocation(name), value);
    }

    void GLShaderProgram::SetUniformVec3(const char *name, const glm::vec3 &value) const
    {
        glUniform3fv(GetUniformLocation(name), 1, glm::value_ptr(value));
    }

    void GLShaderProgram::SetUniformMat4(const char *name, const glm::mat4 &value) const
    {
        glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(value));
    }

    bool GLShaderProgram::CompileShader(GLenum shader_type,
                                        std::string_view source,
                                        GLuint &out_shader,
                                        std::string &out_error_log)
    {
        out_shader = glCreateShader(shader_type);
        if (out_shader == 0)
        {
            out_error_log = "glCreateShader failed";
            return false;
        }

        const GLchar *raw_source = source.data();
        const auto source_length = static_cast<GLint>(source.size());
        glShaderSource(out_shader, 1, &raw_source, &source_length);
        glCompileShader(out_shader);

        GLint success = GL_FALSE;
        glGetShaderiv(out_shader, GL_COMPILE_STATUS, &success);
        if (success == GL_TRUE)
        {
            return true;
        }

        out_error_log = GetShaderInfoLog(out_shader);
        glDeleteShader(out_shader);
        out_shader = 0;
        return false;
    }

    std::string GLShaderProgram::GetShaderInfoLog(GLuint shader)
    {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        if (length <= 1)
        {
            return {};
        }

        std::vector<char> buffer(static_cast<size_t>(length), '\0');
        glGetShaderInfoLog(shader, length, nullptr, buffer.data());
        return std::string(buffer.data());
    }

    std::string GLShaderProgram::GetProgramInfoLog(GLuint program)
    {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        if (length <= 1)
        {
            return {};
        }

        std::vector<char> buffer(static_cast<size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, buffer.data());
        return std::string(buffer.data());
    }

} // namespace hybrid::renderer
