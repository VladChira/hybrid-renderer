#pragma once

#include "renderer/RendererTypes.h"

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

    struct RayTracedPrimaryAlbedoPassInput
    {
        const RenderSettings *settings = nullptr;
        const RenderView *effective_view = nullptr;
        GlTextureId output_texture = 0;
    };

    class RayTracedPrimaryAlbedoPass final
    {
    public:
        RayTracedPrimaryAlbedoPass(GLShaderProgram *program,
                                   GeometryStore *geometry_store,
                                   MaterialStore *material_store,
                                   raytracing::AccelerationStructureCache *as_cache);

        const char *Name() const { return "RayTracedPrimaryAlbedo"; }
        bool Execute(const RayTracedPrimaryAlbedoPassInput &input);

    private:
        GLShaderProgram *m_program = nullptr;
        GeometryStore *m_geometry_store = nullptr;
        MaterialStore *m_material_store = nullptr;
        raytracing::AccelerationStructureCache *m_as_cache = nullptr;
    };

} // namespace hybrid::renderer
