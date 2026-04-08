#pragma once

#include "renderer/RendererTypes.h"

#include <memory>

namespace hybrid::renderer
{
    class GLShaderProgram;
    class GpuSceneResourceCache;

    struct GBufferPassInput
    {
        const RenderSettings *settings = nullptr;
        const FrameSceneData *scene_data = nullptr;
        const RenderView *effective_view = nullptr;
        RendererStats *renderer_stats = nullptr;
        uint32_t gbuffer_framebuffer_id = 0;
        GlTextureId gbuffer_rt0 = 0;
        GlTextureId gbuffer_rt1 = 0;
        GlTextureId gbuffer_entity_id = 0;
        GlTextureId gbuffer_depth = 0;
    };

    struct GBufferPassOutput
    {
        GlTextureId gbuffer_rt0 = 0;
        GlTextureId gbuffer_rt1 = 0;
        GlTextureId gbuffer_entity_id = 0;
        GlTextureId depth = 0;
    };

    class GBufferPass final
    {
    public:
        GBufferPass(GLShaderProgram *gbuffer_shader, GpuSceneResourceCache *gpu_resource_cache);
        ~GBufferPass();

        const char *Name() const;
        bool Execute(const GBufferPassInput &input, GBufferPassOutput &output);

    private:
        struct Impl;
        GLShaderProgram *m_gbuffer_shader = nullptr;
        GpuSceneResourceCache *m_gpu_resource_cache = nullptr;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer
