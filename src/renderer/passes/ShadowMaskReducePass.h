#pragma once

#include "renderer/RendererTypes.h"

namespace hybrid::renderer
{
    class GLShaderProgram;

    struct ShadowMaskReducePassInput
    {
        GlTextureId shadow_mask_array = 0;
        GlTextureId occlusion_out     = 0;
        RenderExtent extent{};
    };

    class ShadowMaskReducePass final
    {
    public:
        explicit ShadowMaskReducePass(GLShaderProgram *program);

        const char *Name() const { return "ShadowMaskReduce"; }
        bool Execute(const ShadowMaskReducePassInput &input);

    private:
        GLShaderProgram *m_program = nullptr;
    };

} // namespace hybrid::renderer
