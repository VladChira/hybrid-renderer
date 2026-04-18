#pragma once

#include "renderer/RendererTypes.h"

#include <cstdint>

#include <glm/glm.hpp>

namespace hybrid::renderer
{
    class GLShaderProgram;
    class LightStore;
}

namespace hybrid::renderer
{

    struct ShadowDenoisePassInput
    {
        const RenderSettings *settings = nullptr;
        const RenderView     *effective_view = nullptr;
        const LightStore     *light_store = nullptr;
        GlTextureId           gbuffer_depth = 0;
        GlTextureId           gbuffer_rt1 = 0;
        GlTextureId           shadow_mask_array = 0;
        GlTextureId           history_current  = 0;  // this-frame target for temporal output
        GlTextureId           history_prev     = 0;  // last-frame output, read as history
        GlTextureId           filter_ping[2]   = {0, 0};

        glm::mat4             prev_view_projection{1.0f};
        bool                  history_valid = false;
    };

    struct ShadowDenoisePassOutput
    {
        GlTextureId filtered_mask_array = 0;
    };

    // Runs temporal + à-trous denoising over every active shadow mask layer.
    // Output is the final filtered array texture for deferred lighting to
    // sample. Number of à-trous iterations is driven by render settings.
    class ShadowDenoisePass final
    {
    public:
        ShadowDenoisePass(GLShaderProgram *temporal_program,
                          GLShaderProgram *atrous_program);

        const char *Name() const { return "ShadowDenoise"; }
        bool Execute(const ShadowDenoisePassInput &input, ShadowDenoisePassOutput &output);

    private:
        GLShaderProgram *m_temporal_program = nullptr;
        GLShaderProgram *m_atrous_program   = nullptr;
    };

} // namespace hybrid::renderer
