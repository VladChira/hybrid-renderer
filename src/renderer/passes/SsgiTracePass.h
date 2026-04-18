#pragma once

#include "renderer/RendererTypes.h"

#include <cstdint>

namespace hybrid::renderer
{
    class GLShaderProgram;
    class GeometryStore;
    class MaterialStore;
}

namespace hybrid::renderer::raytracing
{
    class AccelerationStructureCache;
}

namespace hybrid::renderer
{

    struct SsgiTracePassInput
    {
        const RenderSettings *settings = nullptr;
        const RenderView     *effective_view = nullptr;
        GlTextureId           gbuffer_depth = 0;
        GlTextureId           gbuffer_rt1 = 0;
        GlTextureId           scene_radiance_prev = 0;
        GlTextureId           irradiance_cubemap = 0;
        bool                  has_irradiance = false;
        float                 skybox_intensity = 1.0f;
        float                 skybox_yaw_radians = 0.0f;
        GlTextureId           ssgi_raw_texture = 0;
        uint32_t              frame_index = 0;
    };

    class SsgiTracePass final
    {
    public:
        SsgiTracePass(GLShaderProgram                              *program,
                      GeometryStore                                *geometry_store,
                      MaterialStore                                *material_store,
                      raytracing::AccelerationStructureCache       *as_cache);

        const char *Name() const { return "SsgiTrace"; }
        bool Execute(const SsgiTracePassInput &input);

    private:
        GLShaderProgram                        *m_program        = nullptr;
        GeometryStore                          *m_geometry_store = nullptr;
        MaterialStore                          *m_material_store = nullptr;
        raytracing::AccelerationStructureCache *m_as_cache       = nullptr;
    };

} // namespace hybrid::renderer
