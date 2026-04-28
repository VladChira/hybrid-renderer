#pragma once

#include "renderer/RendererTypes.h"

#include <cstdint>

namespace hybrid::renderer
{
    class GLShaderProgram;

    struct ShadowMaskUpscalePassInput
    {
        RenderExtent input_extent{};   // half-res
        RenderExtent output_extent{};  // full-res
        GlTextureId input_mask_array = 0;
        GlTextureId output_mask_array = 0;
        GlTextureId gbuffer_rt1 = 0;
        GlTextureId gbuffer_depth = 0;
        uint32_t layer_count = 0;
        float depth_sigma = 0.05f;
        float normal_exponent = 32.0f;
    };

    // Joint-bilateral upscale of a low-res shadow mask array back to the full
    // gbuffer resolution. Each output pixel is the bilateral-weighted blend of
    // the 4 surrounding low-res taps using full-res depth + normal as guidance.
    class ShadowMaskUpscalePass final
    {
    public:
        explicit ShadowMaskUpscalePass(GLShaderProgram *program);

        const char *Name() const { return "ShadowMaskUpscale"; }
        bool Execute(const ShadowMaskUpscalePassInput &input);

    private:
        GLShaderProgram *m_program = nullptr;
    };

} // namespace hybrid::renderer
