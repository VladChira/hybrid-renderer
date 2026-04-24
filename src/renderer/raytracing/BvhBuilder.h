#pragma once

#include "core/scene/types/SceneAssets.h"
#include "renderer/raytracing/Bvh.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace hybrid::renderer::raytracing
{

    struct BvhBuildConfig
    {
        BvhSplitStrategyKind split_strategy = BvhSplitStrategyKind::MiddleSplit;
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

    // Read-only input view for split strategy selection at a single node.
    struct BvhSplitRequest
    {
        const std::vector<BvhInput>    *inputs             = nullptr;
        const std::vector<uint32_t>    *primitive_indices  = nullptr;
        uint32_t                        first_primitive    = 0;
        uint32_t                        primitive_count    = 0;
        uint32_t                        depth              = 0;
        core::scene::Aabb               primitive_bounds{};
        core::scene::Aabb               centroid_bounds{};
    };

    struct BvhSplitDecision
    {
        bool     valid          = false;
        uint32_t axis           = 0;      // 0=x, 1=y, 2=z
        float    split_position = 0.0f;   // world-space along `axis`
    };

    class IBvhSplitStrategy
    {
    public:
        virtual ~IBvhSplitStrategy() = default;

        virtual BvhSplitStrategyKind Kind() const = 0;
        virtual const char *DebugName() const = 0;
        virtual BvhSplitDecision ChooseSplit(const BvhSplitRequest &request, const BvhBuildConfig &config) const = 0;
    };

    // Returns a non-owning strategy singleton for the requested kind.
    const IBvhSplitStrategy &GetBvhSplitStrategy(BvhSplitStrategyKind kind);
    std::unique_ptr<IBvhSplitStrategy> CreateBvhSplitStrategy(BvhSplitStrategyKind kind);

    // Shared BVH builder flow. The split policy is selectable by config
    // or can be injected explicitly via `split_strategy_override`.
    BvhBuildResult BuildBvh(const std::vector<BvhInput> &inputs,
                            const BvhBuildConfig &config = {},
                            const IBvhSplitStrategy *split_strategy_override = nullptr);

    // BLAS convenience wrapper - enumerates triangles from a mesh primitive,
    // computes per-triangle bounds and centroids, then calls BuildBvh.
    Blas BuildBlas(const core::scene::MeshPrimitive &primitive,
                   const BvhBuildConfig &config = {},
                   const IBvhSplitStrategy *split_strategy_override = nullptr);

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

    // Moller-Trumbore triangle intersection. Returns true on hit
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
