#pragma once

#include "renderer/RendererTypes.h"

#include <cstdint>

#include <glm/glm.hpp>

namespace hybrid::renderer
{
    class GLShaderProgram;
}

namespace hybrid::renderer
{

    struct SsgiDenoisePassInput
    {
        const RenderSettings *settings = nullptr;
        const RenderView     *effective_view = nullptr;
        GlTextureId           gbuffer_depth = 0;
        GlTextureId           gbuffer_rt1 = 0;
        GlTextureId           ssgi_raw = 0;
        GlTextureId           history_current = 0;
        GlTextureId           history_prev = 0;
        GlTextureId           filter_ping[2] = {0, 0};

        glm::mat4             prev_view_projection{1.0f};
        bool                  history_valid = false;
    };

    struct SsgiDenoisePassOutput
    {
        GlTextureId filtered = 0;
    };

    class SsgiDenoisePass final
    {
    public:
        SsgiDenoisePass(GLShaderProgram *temporal_program,
                        GLShaderProgram *atrous_program);

        const char *Name() const { return "SsgiDenoise"; }
        bool Execute(const SsgiDenoisePassInput &input, SsgiDenoisePassOutput &output);

    private:
        GLShaderProgram *m_temporal_program = nullptr;
        GLShaderProgram *m_atrous_program   = nullptr;
    };

} // namespace hybrid::renderer
