#pragma once

#include "renderer/opengl/GLShaderProgram.h"

#include <string>
#include <unordered_set>

namespace hybrid::renderer
{

    class ShaderManager
    {
    public:
        ShaderManager();

        bool CompileProgramFromFiles(const std::string &vertex_shader_name,
                                     const std::string &fragment_shader_name,
                                     GLShaderProgram &out_program) const;
        bool CompileComputeProgramFromFile(const std::string &compute_shader_name,
                                           GLShaderProgram &out_program) const;

        // Resolves `#include "path"` directives by inlining the referenced
        // file's contents (lookup is relative to the shader root). Returns
        // true on success. Exposed for tests and tooling; the Compile* entry
        // points call it internally.
        bool ResolveIncludes(const std::string &source,
                             const std::string &origin_name,
                             std::string &out_resolved) const;

    private:
        bool ResolveIncludesImpl(const std::string &source,
                                 const std::string &origin_name,
                                 std::unordered_set<std::string> &include_stack,
                                 std::string &out_resolved) const;

        std::string m_shader_root;
    };

} // namespace hybrid::renderer
