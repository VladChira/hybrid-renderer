#pragma once

#include "renderer/RendererTypes.h"

namespace hybrid::renderer
{
    class GLShaderProgram;
    class GeometryStore;
    class LightStore;
    class MaterialStore;

    namespace raytracing { class AccelerationStructureCache; }

    struct RayTracedReflectionPassInput
    {
        const RenderSettings *settings      = nullptr;
        const RenderView     *effective_view = nullptr;

        GlTextureId gbuffer_rt0          = 0;
        GlTextureId gbuffer_rt1          = 0;
        GlTextureId gbuffer_depth        = 0;
        GlTextureId shadow_occlusion     = 0;
        GlTextureId reflection_radiance_out = 0;

        uint32_t frame_index = 0;

        bool        has_skybox          = false;
        GlTextureId skybox_cubemap      = 0;
        GlTextureId irradiance_cubemap  = 0;
        GlTextureId prefiltered_cubemap = 0;
        GlTextureId brdf_lut            = 0;
        float       skybox_intensity    = 1.0f;
        float       skybox_yaw_radians  = 0.0f;
    };

    class RayTracedReflectionPass final
    {
    public:
        RayTracedReflectionPass(GLShaderProgram *program,
                                GeometryStore *geometry_store,
                                MaterialStore *material_store,
                                LightStore *light_store,
                                raytracing::AccelerationStructureCache *as_cache);

        const char *Name() const { return "RayTracedReflection"; }
        bool Execute(const RayTracedReflectionPassInput &input);

    private:
        GLShaderProgram                       *m_program        = nullptr;
        GeometryStore                         *m_geometry_store = nullptr;
        MaterialStore                         *m_material_store = nullptr;
        LightStore                            *m_light_store    = nullptr;
        raytracing::AccelerationStructureCache *m_as_cache      = nullptr;
    };

} // namespace hybrid::renderer
