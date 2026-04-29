#pragma once

#include "renderer/RendererTypes.h"
#include "renderer/raytracing/Bvh.h"
#include "renderer/raytracing/BvhBuilder.h"
#include "renderer/opengl/GLBuffer.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace hybrid::renderer
{
    class GeometryStore;
}

namespace hybrid::renderer::raytracing
{

    struct AccelerationStructureStats
    {
        // BLAS totals
        uint32_t blas_count           = 0;
        uint32_t blas_total_nodes     = 0;
        uint32_t blas_total_leaves    = 0;
        uint32_t blas_total_triangles = 0;
        uint32_t blas_max_depth       = 0;
        double   blas_build_ms_total  = 0.0;

        // TLAS (rebuilt every frame that instances change)
        uint32_t tlas_nodes     = 0;
        uint32_t tlas_leaves    = 0;
        uint32_t tlas_max_depth = 0;
        uint32_t tlas_instances = 0;
        double   tlas_build_ms  = 0.0;

        // GPU footprint estimate (bytes).
        size_t gpu_blas_nodes_bytes     = 0;
        size_t gpu_blas_triangles_bytes = 0;
        size_t gpu_tlas_nodes_bytes     = 0;
        size_t gpu_tlas_instances_bytes = 0;
    };

    // Owns all acceleration-structure state. Per-primitive BLAS are cached
    // across frames; TLAS is rebuilt any frame the scene has new instances or
    // transforms have changed. GPU upload is full re-write for simplicity.
    class AccelerationStructureCache
    {
    public:
        AccelerationStructureCache();

        // Creates the GL buffer objects. Must be called after GLAD init.
        bool Init();

        // Ensures a BLAS exists for every primitive referenced by `scene`.
        // For any newly-built BLAS, patches the primitive descriptor in
        // `geometry_store` with blas_root + blas_triangle_offset. Idempotent.
        void SyncBlas(const FrameSceneData &scene, GeometryStore &geometry_store);

        // Rebuilds the TLAS from the current frame's mesh instances (opaque +
        // masked). One TLAS instance is emitted per (mesh instance, primitive).
        void SyncTlas(const FrameSceneData &scene, const GeometryStore &geometry_store);

        // Pushes any dirty CPU-side buffers to GPU SSBOs. Returns true when
        // the GPU side is ready for ray dispatches.
        bool Upload();

        // Binds the four AS SSBOs at their canonical binding slots.
        void BindSsbos() const;

        const AccelerationStructureStats &Stats() const { return m_stats; }

        // CPU-side accessors. The Vulkan path mirrors these vectors into its
        // own SSBOs since it does not call Upload() (GL-only). Layouts are
        // std430-compatible (BvhNode = 32 B, GpuTlasInstance = 144 B,
        // BLAS-triangle = uint).
        const std::vector<BvhNode>           &BlasNodes()      const { return m_blas_nodes; }
        const std::vector<uint32_t>          &BlasTriangles()  const { return m_blas_triangles; }
        const std::vector<BvhNode>           &TlasNodes()      const { return m_tlas.nodes; }
        const std::vector<GpuTlasInstance>   &TlasInstances()  const { return m_tlas.instances; }

        // Reserves (at least) `primitive_count` primitive slots in the cache.
        // Not required — append grows on demand — but useful for tests and
        // pre-allocation.
        void Reserve(size_t primitive_count);

        void Clear();

    private:
        struct BlasKey
        {
            uint64_t mesh_id = 0;
            uint32_t primitive_index = 0;
            bool operator==(const BlasKey &o) const { return mesh_id == o.mesh_id && primitive_index == o.primitive_index; }
        };
        struct BlasKeyHash
        {
            size_t operator()(const BlasKey &k) const noexcept;
        };
        struct BlasRecord
        {
            uint32_t node_offset     = 0;  // index into m_blas_nodes
            uint32_t node_count      = 0;
            uint32_t triangle_offset = 0;  // index into m_blas_triangles
            uint32_t triangle_count  = 0;
            uint32_t primitive_id    = 0;  // GeometryStore primitive_id patched with blas_root
            BvhBuildStats stats{};
            core::scene::Aabb bounds{};
        };

        // Ensures a GeometryStore primitive has an entry in the BLAS cache.
        // Builds a fresh BLAS when missing, concatenates its data into the
        // global buffers, and patches the primitive descriptor.
        bool EnsurePrimitiveBlas(uint64_t mesh_id,
                                 uint32_t primitive_index,
                                 const core::scene::MeshPrimitive &primitive,
                                 uint32_t primitive_id,
                                 GeometryStore &geometry_store);

        std::unordered_map<BlasKey, BlasRecord, BlasKeyHash> m_blas_records;

        // Global concatenated BLAS storage.
        std::vector<BvhNode>  m_blas_nodes;
        std::vector<uint32_t> m_blas_triangles;

        // TLAS built per frame (or when instance set changes).
        Tlas m_tlas;

        // GPU buffers
        GLBuffer m_blas_nodes_buffer{};
        GLBuffer m_blas_triangles_buffer{};
        GLBuffer m_tlas_nodes_buffer{};
        GLBuffer m_tlas_instances_buffer{};

        size_t m_gpu_blas_nodes_capacity     = 0;
        size_t m_gpu_blas_triangles_capacity = 0;
        size_t m_gpu_tlas_nodes_capacity     = 0;
        size_t m_gpu_tlas_instances_capacity = 0;

        bool m_blas_dirty = false;
        bool m_tlas_dirty = false;

        AccelerationStructureStats m_stats{};
        bool m_initialized = false;
    };

} // namespace hybrid::renderer::raytracing
