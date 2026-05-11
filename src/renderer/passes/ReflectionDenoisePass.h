#pragma once

#include "renderer/RendererTypes.h"

#include <cstdint>

#include <glm/glm.hpp>

namespace hybrid::renderer
{
    class GLShaderProgram;

    struct ReflectionDenoisePassInput
    {
        const RenderView *effective_view = nullptr;
        RenderExtent      extent{};

        GlTextureId current_radiance   = 0;   // rgba16f — raw reflection output
        GlTextureId history_prev       = 0;   // rgba16f — previous frame's history
        GlTextureId history_out        = 0;   // rgba16f — write temporal result here
        GlTextureId atrous_ping        = 0;   // rgba16f — scratch for A-Trous ping
        GlTextureId atrous_pong        = 0;   // rgba16f — scratch for A-Trous pong
        GlTextureId gbuffer_rt1        = 0;
        GlTextureId gbuffer_depth      = 0;
        GlTextureId gbuffer_rt1_prev   = 0;
        GlTextureId gbuffer_depth_prev = 0;

        bool        history_valid        = false;
        glm::mat4   prev_view_projection{1.0f};
        float       camera_near          = 0.1f;
        float       camera_far           = 1000.0f;
        float       depth_tolerance      = 0.05f;
        float       normal_tolerance     = 0.9f;
        float       temporal_alpha       = 0.9f;
        uint32_t    atrous_iterations    = 3;
        float       c_phi                = 0.15f;
        float       n_phi                = 32.0f;
        float       p_phi                = 0.02f;
    };

    struct ReflectionDenoisePassOutput
    {
        GlTextureId denoised = 0;
    };

    class ReflectionDenoisePass final
    {
    public:
        ReflectionDenoisePass(GLShaderProgram *temporal_program,
                              GLShaderProgram *atrous_program);

        const char *Name() const { return "ReflectionDenoise"; }
        bool Execute(const ReflectionDenoisePassInput &input,
                     ReflectionDenoisePassOutput      &output);

    private:
        GLShaderProgram *m_temporal_program = nullptr;
        GLShaderProgram *m_atrous_program   = nullptr;
    };

} // namespace hybrid::renderer
