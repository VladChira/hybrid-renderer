#pragma once

#include "renderer/opengl/GLShaderProgram.h"

#include <string>

namespace hybrid::renderer
{

    class ShaderManager
    {
    public:
        ShaderManager();

        bool CompileProgramFromFiles(const std::string &vertex_shader_name,
                                     const std::string &fragment_shader_name,
                                     GLShaderProgram &out_program) const;

    private:
        std::string m_shader_root;
    };

} // namespace hybrid::renderer
