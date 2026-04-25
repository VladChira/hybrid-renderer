#include "renderer/ShaderManager.h"

#include "core/Log.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>

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

        std::filesystem::path NormalizePath(const std::filesystem::path &path)
        {
            std::error_code ec;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
            if (!ec)
            {
                return canonical;
            }
            return path.lexically_normal();
        }

        bool TryParseInclude(const std::string &line, std::string &out_include_path)
        {
            out_include_path.clear();

            size_t pos = 0;
            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
            {
                ++pos;
            }
            if (pos >= line.size() || line[pos] != '#')
            {
                return false;
            }
            ++pos;

            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
            {
                ++pos;
            }

            constexpr std::string_view kIncludeToken = "include";
            if (line.compare(pos, kIncludeToken.size(), kIncludeToken) != 0)
            {
                return false;
            }
            pos += kIncludeToken.size();

            while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
            {
                ++pos;
            }
            if (pos >= line.size() || line[pos] != '"')
            {
                return false;
            }
            ++pos;

            const size_t end_quote = line.find('"', pos);
            if (end_quote == std::string::npos || end_quote == pos)
            {
                return false;
            }

            out_include_path = line.substr(pos, end_quote - pos);
            return true;
        }

        bool ResolveIncludePath(const std::filesystem::path &including_file,
                                const std::filesystem::path &shader_root,
                                const std::string &include_token,
                                std::filesystem::path &out_resolved_path)
        {
            const std::filesystem::path local_candidate = including_file.parent_path() / include_token;
            if (std::filesystem::exists(local_candidate))
            {
                out_resolved_path = NormalizePath(local_candidate);
                return true;
            }

            const std::filesystem::path root_candidate = shader_root / include_token;
            if (std::filesystem::exists(root_candidate))
            {
                out_resolved_path = NormalizePath(root_candidate);
                return true;
            }

            return false;
        }

        bool LoadShaderSourceRecursive(const std::filesystem::path &file_path,
                                       const std::filesystem::path &shader_root,
                                       std::vector<std::string> &include_stack,
                                       std::string &out_source)
        {
            const std::filesystem::path normalized_file = NormalizePath(file_path);
            const std::string normalized_string = normalized_file.generic_string();

            if (std::find(include_stack.begin(), include_stack.end(), normalized_string) != include_stack.end())
            {
                LOG_ERROR("[ShaderManager] Include cycle detected while loading '{}'", normalized_string);
                return false;
            }

            std::string source;
            if (!ReadTextFile(normalized_string, source))
            {
                return false;
            }

            include_stack.push_back(normalized_string);
            out_source.clear();

            std::istringstream stream(source);
            std::string line;
            while (std::getline(stream, line))
            {
                std::string include_token;
                if (TryParseInclude(line, include_token))
                {
                    std::filesystem::path include_path;
                    if (!ResolveIncludePath(normalized_file, shader_root, include_token, include_path))
                    {
                        LOG_ERROR("[ShaderManager] Failed to resolve include '{}' from '{}'",
                                  include_token,
                                  normalized_string);
                        include_stack.pop_back();
                        return false;
                    }

                    std::string included_source;
                    if (!LoadShaderSourceRecursive(include_path, shader_root, include_stack, included_source))
                    {
                        include_stack.pop_back();
                        return false;
                    }

                    out_source += included_source;
                    if (!out_source.empty() && out_source.back() != '\n')
                    {
                        out_source.push_back('\n');
                    }
                }
                else
                {
                    out_source += line;
                    out_source.push_back('\n');
                }
            }

            include_stack.pop_back();
            return true;
        }

        bool LoadShaderSourceWithIncludes(const std::string &path,
                                          const std::string &shader_root,
                                          std::string &out_source)
        {
            std::vector<std::string> include_stack;
            return LoadShaderSourceRecursive(std::filesystem::path(path),
                                             std::filesystem::path(shader_root),
                                             include_stack,
                                             out_source);
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
        if (!LoadShaderSourceWithIncludes(vertex_path, m_shader_root, vertex_source))
        {
            return false;
        }

        LOG_INFO("[ShaderManager] Loading fragment shader from '{}'", fragment_path);
        if (!LoadShaderSourceWithIncludes(fragment_path, m_shader_root, fragment_source))
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

    bool ShaderManager::CompileComputeProgramFromFile(const std::string &compute_shader_name,
                                                      GLShaderProgram &out_program) const
    {
        const std::string compute_path = m_shader_root + "/" + compute_shader_name;

        std::string compute_source;
        LOG_INFO("[ShaderManager] Loading compute shader from '{}'", compute_path);
        if (!LoadShaderSourceWithIncludes(compute_path, m_shader_root, compute_source))
        {
            return false;
        }

        LOG_INFO("[ShaderManager] Compiling compute shader program (compute='{}')",
                 compute_shader_name);
        if (!out_program.LinkComputeFromSource(compute_source))
        {
            LOG_ERROR("[ShaderManager] Compute shader compilation/linking failed (compute='{}')",
                      compute_path);
            return false;
        }

        LOG_INFO("[ShaderManager] Compute shader program compiled successfully (compute='{}')",
                 compute_shader_name);
        return true;
    }

} // namespace hybrid::renderer
