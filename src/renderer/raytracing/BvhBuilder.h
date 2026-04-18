#pragma once

#include "core/scene/types/SceneAssets.h"
#include "renderer/raytracing/Bvh.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace hybrid::renderer::raytracing
{

    struct BvhBuildConfig
    {
        uint32_t max_leaf_primitives = 4;
        uint32_t max_depth           = 64;
        uint32_t sah_bucket_count    = 16;
    };

    // Input record for the generic builder. `payload_index` is the caller's
    // opaque identifier (a triangle index for BLAS, an instance index for
    // TLAS) that gets written into the output's primitive table.
    struct BvhInput
    {
        core::scene::Aabb bounds;
        glm::vec3         centroid;
        uint32_t          payload_index;
    };

    struct BvhBuildResult
    {
        std::vector<BvhNode>  nodes;
        std::vector<uint32_t> primitive_indices;  // payload_index permuted into leaf order
        core::scene::Aabb     bounds;
        BvhBuildStats         stats;
    };

    // Top-down SAH builder with bucket binning. Input vector is not modified.
    BvhBuildResult BuildBvh(const std::vector<BvhInput> &inputs, const BvhBuildConfig &config);

    // BLAS convenience wrapper — enumerates triangles from a mesh primitive,
    // computes per-triangle bounds and centroids, then calls BuildBvh.
    Blas BuildBlas(const core::scene::MeshPrimitive &primitive, const BvhBuildConfig &config = {});

    // CPU ray-box test, shared by the verification tests and any host-side
    // traversal (e.g. picking without the GPU path). Returns true if the ray
    // hits the AABB, writing the near t into `out_t_near`.
    bool IntersectRayAabb(const glm::vec3 &origin,
                          const glm::vec3 &inv_direction,
                          const glm::vec3 &bmin,
                          const glm::vec3 &bmax,
                          float t_min,
                          float t_max,
                          float &out_t_near);

    // Watertight Möller–Trumbore triangle intersection. Returns true on hit
    // and writes distance + barycentrics.
    bool IntersectRayTriangle(const glm::vec3 &origin,
                              const glm::vec3 &direction,
                              const glm::vec3 &v0,
                              const glm::vec3 &v1,
                              const glm::vec3 &v2,
                              float t_min,
                              float t_max,
                              float &out_t,
                              float &out_u,
                              float &out_v);

} // namespace hybrid::renderer::raytracing
