#pragma once

#include "renderer/RendererTypes.h"

#include <memory>

namespace hybrid::renderer
{
    class GLShaderProgram;

    struct DeferredLightingPassInput
    {
        const RenderSettings *settings = nullptr;
        const FrameSceneData *scene_data = nullptr;
        const RenderView *effective_view = nullptr;
        uint32_t scene_framebuffer_id = 0;
        GlTextureId scene_color = 0;
        GlTextureId gbuffer_rt0 = 0;
        GlTextureId gbuffer_rt1 = 0;
        GlTextureId gbuffer_depth = 0;
    };

    struct DeferredLightingPassOutput
    {
        GlTextureId color = 0;
        GlTextureId depth = 0;
    };

    class DeferredLightingPass final
    {
    public:
        explicit DeferredLightingPass(GLShaderProgram *deferred_shader);
        ~DeferredLightingPass();

        const char *Name() const;
        bool Execute(const DeferredLightingPassInput &input, DeferredLightingPassOutput &output);

    private:
        struct Impl;
        GLShaderProgram *m_deferred_shader = nullptr;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer
