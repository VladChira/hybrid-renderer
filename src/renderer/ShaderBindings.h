#pragma once

#include <glad.h>

// Central registry of SSBO / uniform-buffer binding slots shared by the raster
// and (future) ray-tracing paths. Every GLSL program that reads or writes one
// of these buffers declares `layout(std430, binding = N)` with the same N.
namespace hybrid::renderer::binding
{

    // Geometry (Phase 0.1): global vertex + index store and per-primitive descriptors.
    constexpr GLuint k_geometry_vertices  = 0;
    constexpr GLuint k_geometry_indices   = 1;
    constexpr GLuint k_primitives         = 2;

    // Materials (Phase 0.2): global material table, textures accessed via bindless handles.
    constexpr GLuint k_materials          = 3;

    // Lights (Phase 0.3): one SSBO per light type.
    constexpr GLuint k_directional_lights = 4;
    constexpr GLuint k_point_lights       = 5;
    constexpr GLuint k_area_lights        = 6;

    // Acceleration structures (Phase 1, reserved now to keep numbering stable).
    constexpr GLuint k_blas_nodes         = 7;
    constexpr GLuint k_blas_triangles     = 8;
    constexpr GLuint k_tlas_nodes         = 9;
    constexpr GLuint k_tlas_instances     = 10;

} // namespace hybrid::renderer::binding
