#pragma once

#include "renderer/RendererTypes.h"

#include <cstdint>

namespace hybrid::renderer
{
    class GLShaderProgram;
    class GeometryStore;
    class LightStore;
}

namespace hybrid::renderer::raytracing
{
    class AccelerationStructureCache;
}

namespace hybrid::renderer
{

    struct RayTracedShadowPassInput
    {
        const RenderSettings *settings         = nullptr;
        const RenderView     *effective_view   = nullptr;
        const LightStore     *light_store      = nullptr;
        GlTextureId           gbuffer_depth    = 0;
        GlTextureId           gbuffer_rt1      = 0;
        GlTextureId           shadow_mask_array = 0;
        // Dispatch / shadow-mask extent. Half of render_extent when the
        // half-res shadow path is on; equal to render_extent otherwise.
        RenderExtent          shadow_extent{};
        uint32_t              frame_index      = 0;
    };

    // Writes a per-light visibility mask into a specific layer of the shadow
    // mask 2D array. One `glDispatchCompute` per shadow-casting light.
    class RayTracedShadowPass final
    {
    public:
        RayTracedShadowPass(GLShaderProgram                              *program,
                            GeometryStore                                *geometry_store,
                            raytracing::AccelerationStructureCache       *as_cache);

        const char *Name() const { return "RayTracedShadow"; }
        bool Execute(const RayTracedShadowPassInput &input);

    private:
        GLShaderProgram                        *m_program        = nullptr;
        GeometryStore                          *m_geometry_store = nullptr;
        raytracing::AccelerationStructureCache *m_as_cache       = nullptr;
    };

} // namespace hybrid::renderer
