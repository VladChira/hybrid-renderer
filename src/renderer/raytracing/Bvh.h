#pragma once

#include "core/scene/types/SceneMath.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace hybrid::renderer::raytracing
{

    // std430 compatible BVH node. Works with both
    // BLAS and TLAS. 
    // Leaf encoding: `right_or_count < 0` flags a leaf. Primitive count is
    // `-right_or_count`, first primitive index is `left_or_first`.
    // Internal encoding: `left_or_first` is the left child node index,
    // `right_or_count` is the right child node index.
    struct BvhNode
    {
        glm::vec3 bmin;
        int32_t left_or_first;
        glm::vec3 bmax;
        int32_t right_or_count;
    };
    static_assert(sizeof(BvhNode) == 32, "BvhNode must match std430 layout (32 bytes)");

    inline bool IsLeaf(const BvhNode &n)        { return n.right_or_count < 0; }
    inline uint32_t LeafFirst(const BvhNode &n) { return static_cast<uint32_t>(n.left_or_first); }
    inline uint32_t LeafCount(const BvhNode &n) { return static_cast<uint32_t>(-n.right_or_count); }

    enum class BvhBuildStrategyKind : uint8_t
    {
        MiddleSplit,
        Sah
    };

    struct BvhBuildStats
    {
        BvhBuildStrategyKind strategy = BvhBuildStrategyKind::MiddleSplit;
        uint32_t node_count      = 0;
        uint32_t leaf_count      = 0;
        uint32_t max_depth       = 0;
        uint32_t primitive_count = 0;  // triangles for BLAS, instances for TLAS
        double   build_ms        = 0.0;
    };

    // BLAS: one per (mesh, primitive_index) pair. Built in mesh-local space
    // over the primitive's triangles.
    struct Blas
    {
        std::vector<BvhNode>  nodes;
        // Permutation of triangle indices within the primitive. Leaves store
        // ranges `[LeafFirst, LeafFirst + LeafCount)` into this table; each
        // entry is a triangle index (0 .. triangle_count-1) relative to the
        // primitive. The GPU resolves the actual vertex indices through the
        // global index buffer + per-primitive index_offset.
        std::vector<uint32_t> triangle_indices;
        core::scene::Aabb     bounds;
        BvhBuildStats         stats;
    };

    // std430-compatible TLAS instance.
    struct GpuTlasInstance
    {
        glm::mat4 world_from_local;
        glm::mat4 local_from_world;
        uint32_t  primitive_id;
        uint32_t  entity_id;
        uint32_t  _pad0;
        uint32_t  _pad1;
    };
    static_assert(sizeof(GpuTlasInstance) == 144, "GpuTlasInstance must match std430 layout (144 bytes)");

    struct Tlas
    {
        std::vector<BvhNode>        nodes;
        std::vector<GpuTlasInstance> instances;
        core::scene::Aabb            bounds;
        BvhBuildStats                stats;
    };

} // namespace hybrid::renderer::raytracing
