#pragma once

#include "renderer/RendererTypes.h"

#include <cstdint>

#include <glm/glm.hpp>

namespace hybrid::renderer
{
    class GLShaderProgram;

    struct SpatioTemporalDenoisePassInput
    {
        const RenderView *effective_view = nullptr;
        RenderExtent extent{};

        GlTextureId current_signal_array = 0;
        GlTextureId history_prev_array = 0;
        GlTextureId history_out_array = 0;
        GlTextureId atrous_ping_array = 0;
        GlTextureId atrous_pong_array = 0;
        GlTextureId gbuffer_rt1 = 0;
        GlTextureId gbuffer_depth = 0;
        GlTextureId gbuffer_rt1_prev = 0;
        GlTextureId gbuffer_depth_prev = 0;

        uint32_t layer_count = 0;
        uint32_t denoise_layer_mask = 0xFFFFFFFFu;
        bool history_valid = false;
        glm::mat4 prev_view_projection{1.0f};
        float camera_near = 0.1f;
        float camera_far = 1000.0f;
        // Disocclusion guards on history reuse.
        float depth_tolerance = 0.05f;
        float normal_tolerance = 0.9f;
        float temporal_alpha = 0.9f;
        uint32_t atrous_iterations = 3;
        float c_phi = 0.15f;
        float n_phi = 32.0f;
        float p_phi = 0.02f;
    };

    struct SpatioTemporalDenoisePassOutput
    {
        GlTextureId history = 0;
        GlTextureId denoised = 0;
    };

    // Generic spatio-temporal denoiser for layered scalar signals.
    // Temporal stage: reprojection + alpha blend into history.
    // Spatial stage: edge-aware A-Trous filtering guided by normal/depth.
    class SpatioTemporalDenoisePass final
    {
    public:
        SpatioTemporalDenoisePass(GLShaderProgram *temporal_program,
                                  GLShaderProgram *atrous_program);

        const char *Name() const { return "SpatioTemporalDenoise"; }
        bool Execute(const SpatioTemporalDenoisePassInput &input,
                     SpatioTemporalDenoisePassOutput &output);

    private:
        GLShaderProgram *m_temporal_program = nullptr;
        GLShaderProgram *m_atrous_program = nullptr;
    };

} // namespace hybrid::renderer
