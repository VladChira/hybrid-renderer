#pragma once

#include "renderer/RendererTypes.h"

#include <memory>

namespace hybrid::renderer
{
    class GLShaderProgram;
    class GeometryStore;
}

namespace hybrid::renderer::raytracing
{
    class AccelerationStructureCache;
}

namespace hybrid::renderer
{

    struct TraversalHeatmapPassInput
    {
        const RenderSettings *settings = nullptr;
        const RenderView     *effective_view = nullptr;
        GlTextureId           heatmap_texture = 0;
    };

    // Dispatches a compute shader that traces one primary ray per pixel
    // through the TLAS/BLAS and writes a false-coloured node-visit count into
    // `heatmap_texture`. Validates AS correctness end-to-end.
    class TraversalHeatmapPass final
    {
    public:
        TraversalHeatmapPass(GLShaderProgram                              *program,
                             GeometryStore                                *geometry_store,
                             raytracing::AccelerationStructureCache       *as_cache);

        const char *Name() const { return "TraversalHeatmap"; }
        bool Execute(const TraversalHeatmapPassInput &input);

    private:
        GLShaderProgram                        *m_program        = nullptr;
        GeometryStore                          *m_geometry_store = nullptr;
        raytracing::AccelerationStructureCache *m_as_cache       = nullptr;
    };

} // namespace hybrid::renderer
