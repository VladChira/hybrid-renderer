#pragma once

#include "renderer/RendererTypes.h"

#include <memory>

namespace hybrid::renderer
{
    class GLShaderProgram;

    struct ForwardPassInput
    {
        const RenderSettings *settings = nullptr;
        const FrameSceneData *scene_data = nullptr;
        const RenderView *effective_view = nullptr;
        RendererStats *stats = nullptr;
        uint32_t scene_framebuffer_id = 0;
        GlTextureId scene_color = 0;
        GlTextureId gbuffer_rt0 = 0;
        GlTextureId gbuffer_rt1 = 0;
        GlTextureId gbuffer_entity_id = 0;
        GlTextureId gbuffer_depth = 0;
    };

    struct ForwardPassOutput
    {
        GlTextureId color = 0;
        GlTextureId depth = 0;
        GlTextureId gbuffer_rt0 = 0;
        GlTextureId gbuffer_rt1 = 0;
        GlTextureId gbuffer_entity_id = 0;
    };

    class ForwardLitPass final
    {
    public:
        explicit ForwardLitPass(GLShaderProgram *forward_shader);
        ~ForwardLitPass();

        const char *Name() const;
        bool Execute(const ForwardPassInput &input, ForwardPassOutput &output);

    private:
        struct Impl;
        GLShaderProgram *m_forward_shader = nullptr;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer
