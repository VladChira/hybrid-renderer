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

    struct RayTracedAlbedoPassInput
    {
        const RenderSettings *settings = nullptr;
        const RenderView     *effective_view = nullptr;
        GlTextureId           albedo_texture = 0;
    };

    // Phase 2 exit criterion: traces primary rays and writes first-hit base
    // colour into `albedo_texture`. Visual A/B against G-buffer RT0 proves
    // the BLAS / TLAS / geometry + material fetch path.
    class RayTracedAlbedoPass final
    {
    public:
        RayTracedAlbedoPass(GLShaderProgram                              *program,
                            GeometryStore                                *geometry_store,
                            MaterialStore                                *material_store,
                            raytracing::AccelerationStructureCache       *as_cache);

        const char *Name() const { return "RayTracedAlbedo"; }
        bool Execute(const RayTracedAlbedoPassInput &input);

    private:
        GLShaderProgram                        *m_program        = nullptr;
        GeometryStore                          *m_geometry_store = nullptr;
        MaterialStore                          *m_material_store = nullptr;
        raytracing::AccelerationStructureCache *m_as_cache       = nullptr;
    };

} // namespace hybrid::renderer
