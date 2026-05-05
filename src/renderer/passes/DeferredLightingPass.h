#pragma once

#include "renderer/RendererTypes.h"

#include <memory>

namespace hybrid::renderer
{
    class GLShaderProgram;
    class LightStore;

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
        bool has_skybox = false;
        GlTextureId skybox_cubemap = 0;
        GlTextureId convoluted_cubemap = 0;
        GlTextureId prefiltered_cubemap = 0;
        GlTextureId brdf_lut = 0;
        float skybox_intensity = 1.0f;
        float skybox_yaw_radians = 0.0f;
        GlTextureId reflection_texture = 0;

        GlTextureId shadow_mask_array = 0;
    };

    struct DeferredLightingPassOutput
    {
        GlTextureId color = 0;
        GlTextureId depth = 0;
    };

    class DeferredLightingPass final
    {
    public:
        DeferredLightingPass(GLShaderProgram *deferred_shader, LightStore *light_store);
        ~DeferredLightingPass();

        const char *Name() const;
        bool Execute(const DeferredLightingPassInput &input, DeferredLightingPassOutput &output);

    private:
        struct Impl;
        GLShaderProgram *m_deferred_shader = nullptr;
        LightStore      *m_light_store = nullptr;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer
