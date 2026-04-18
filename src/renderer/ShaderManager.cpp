#include "renderer/ShaderManager.h"

#include "core/Log.h"

#include <algorithm>
#include <cctype>
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

        std::string resolved_vertex;
        std::string resolved_fragment;
        if (!ResolveIncludes(vertex_source, vertex_shader_name, resolved_vertex) ||
            !ResolveIncludes(fragment_source, fragment_shader_name, resolved_fragment))
        {
            return false;
        }

        LOG_INFO("[ShaderManager] Compiling shader program (vertex='{}', fragment='{}')",
                 vertex_shader_name,
                 fragment_shader_name);
        if (!out_program.LinkFromSource(resolved_vertex, resolved_fragment))
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
        if (!ReadTextFile(compute_path, compute_source))
        {
            return false;
        }

        std::string resolved;
        if (!ResolveIncludes(compute_source, compute_shader_name, resolved))
        {
            return false;
        }

        LOG_INFO("[ShaderManager] Compiling compute shader program (compute='{}')",
                 compute_shader_name);
        if (!out_program.LinkComputeFromSource(resolved))
        {
            LOG_ERROR("[ShaderManager] Compute shader compilation/linking failed (compute='{}')",
                      compute_path);
            return false;
        }

        LOG_INFO("[ShaderManager] Compute shader program compiled successfully (compute='{}')",
                 compute_shader_name);
        return true;
    }

    bool ShaderManager::ResolveIncludes(const std::string &source,
                                        const std::string &origin_name,
                                        std::string &out_resolved) const
    {
        std::unordered_set<std::string> include_stack;
        return ResolveIncludesImpl(source, origin_name, include_stack, out_resolved);
    }

    bool ShaderManager::ResolveIncludesImpl(const std::string &source,
                                            const std::string &origin_name,
                                            std::unordered_set<std::string> &include_stack,
                                            std::string &out_resolved) const
    {
        // Bookkeeping for cycle detection. Use the origin's canonical name.
        if (!origin_name.empty() && !include_stack.insert(origin_name).second)
        {
            LOG_ERROR("[ShaderManager] Include cycle detected at '{}'", origin_name);
            return false;
        }

        std::istringstream stream(source);
        std::string line;
        std::ostringstream output;

        while (std::getline(stream, line))
        {
            // Detect `^\s*#\s*include\s+"..."` robustly without pulling in a
            // regex library.
            size_t cursor = 0;
            while (cursor < line.size() && std::isspace(static_cast<unsigned char>(line[cursor])))
            {
                ++cursor;
            }
            if (cursor < line.size() && line[cursor] == '#')
            {
                size_t hash_end = cursor + 1;
                while (hash_end < line.size() && std::isspace(static_cast<unsigned char>(line[hash_end])))
                {
                    ++hash_end;
                }
                const std::string keyword = "include";
                if (hash_end + keyword.size() <= line.size() &&
                    line.compare(hash_end, keyword.size(), keyword) == 0 &&
                    (hash_end + keyword.size() == line.size() ||
                     std::isspace(static_cast<unsigned char>(line[hash_end + keyword.size()]))))
                {
                    size_t quote_begin = line.find('"', hash_end + keyword.size());
                    size_t quote_end   = (quote_begin == std::string::npos) ? std::string::npos : line.find('"', quote_begin + 1);
                    if (quote_begin == std::string::npos || quote_end == std::string::npos || quote_end <= quote_begin + 1)
                    {
                        LOG_ERROR("[ShaderManager] Malformed #include in '{}': {}", origin_name, line);
                        include_stack.erase(origin_name);
                        return false;
                    }
                    const std::string relative_path = line.substr(quote_begin + 1, quote_end - quote_begin - 1);
                    const std::string full_path = m_shader_root + "/" + relative_path;

                    std::string include_source;
                    if (!ReadTextFile(full_path, include_source))
                    {
                        LOG_ERROR("[ShaderManager] Failed to resolve #include '{}' from '{}'", relative_path, origin_name);
                        include_stack.erase(origin_name);
                        return false;
                    }

                    std::string resolved_nested;
                    if (!ResolveIncludesImpl(include_source, relative_path, include_stack, resolved_nested))
                    {
                        include_stack.erase(origin_name);
                        return false;
                    }
                    output << "// ==== begin include: " << relative_path << " ====\n";
                    output << resolved_nested;
                    if (!resolved_nested.empty() && resolved_nested.back() != '\n')
                    {
                        output << '\n';
                    }
                    output << "// ==== end include: " << relative_path << " ====\n";
                    continue;
                }
            }

            output << line << '\n';
        }

        out_resolved = output.str();
        include_stack.erase(origin_name);
        return true;
    }

} // namespace hybrid::renderer
