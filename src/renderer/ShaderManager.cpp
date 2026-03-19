#include "renderer/ShaderManager.h"

#include "core/Log.h"

#include <fstream>
#include <sstream>

namespace hybrid::renderer
{

    namespace
    {
        bool ReadTextFile(const std::string &path, std::string &out_source)
        {
            out_source.clear();

            std::ifstream file(path, std::ios::in | std::ios::binary);
            if (!file.is_open())
            {
                LOG_ERROR("[ShaderManager] Failed to open shader file '{}'", path);
                return false;
            }

            std::ostringstream buffer;
            buffer << file.rdbuf();
            out_source = buffer.str();
            if (out_source.empty())
            {
                LOG_WARN("[ShaderManager] Shader file '{}' is empty", path);
            }
            return true;
        }
    } // namespace

    ShaderManager::ShaderManager()
    {
#ifdef HYBRID_PROJECT_ROOT
        m_shader_root = std::string(HYBRID_PROJECT_ROOT) + "/shaders";
#else
        m_shader_root = "shaders";
#endif
    }

    bool ShaderManager::CompileProgramFromFiles(const std::string &vertex_shader_name,
                                                const std::string &fragment_shader_name,
                                                GLShaderProgram &out_program) const
    {
        const std::string vertex_path = m_shader_root + "/" + vertex_shader_name;
        const std::string fragment_path = m_shader_root + "/" + fragment_shader_name;

        std::string vertex_source;
        std::string fragment_source;

        LOG_INFO("[ShaderManager] Loading vertex shader from '{}'", vertex_path);
        if (!ReadTextFile(vertex_path, vertex_source))
        {
            return false;
        }

        LOG_INFO("[ShaderManager] Loading fragment shader from '{}'", fragment_path);
        if (!ReadTextFile(fragment_path, fragment_source))
        {
            return false;
        }

        LOG_INFO("[ShaderManager] Compiling shader program (vertex='{}', fragment='{}')",
                 vertex_shader_name,
                 fragment_shader_name);
        if (!out_program.LinkFromSource(vertex_source, fragment_source))
        {
            LOG_ERROR("[ShaderManager] Shader compilation/linking failed (vertex='{}', fragment='{}')",
                      vertex_path,
                      fragment_path);
            return false;
        }

        LOG_INFO("[ShaderManager] Shader program compiled successfully (vertex='{}', fragment='{}')",
                 vertex_shader_name,
                 fragment_shader_name);
        return true;
    }

} // namespace hybrid::renderer
