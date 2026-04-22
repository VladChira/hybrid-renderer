#pragma once

#include "core/scene/types/SceneAssets.h"
#include "renderer/opengl/GLBuffer.h"
#include "renderer/opengl/GLVertexArray.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace hybrid::renderer
{

    // std430-compatible vertex record. Layout is explicitly padded so the CPU
    // sizeof matches the std430 layout rules (vec3 aligned to 16, followed by
    // 4 bytes of trailing pad). Consumed both by raster (via the shared VAO)
    // and by future compute/ray passes (via SSBO fetch).
    struct GpuVertex
    {
        glm::vec3 position;
        float     _pad_position;
        glm::vec3 normal;
        float     _pad_normal;
        glm::vec4 tangent;
        glm::vec2 uv0;
        glm::vec2 uv1;
        glm::vec4 color0;
    };
    static_assert(sizeof(GpuVertex) == 80, "GpuVertex must match std430 layout (80 bytes)");

    // Per-primitive descriptor. Addresses the shared vertex/index buffers and
    // carries the material-table index. `blas_root` is reserved for acceleration structures.
    struct GpuPrimitive
    {
        uint32_t vertex_offset;
        uint32_t vertex_count;
        uint32_t index_offset;
        uint32_t index_count;
        uint32_t material_index;
        uint32_t blas_root;
        uint32_t _pad0;
        uint32_t _pad1;
    };
    static_assert(sizeof(GpuPrimitive) == 32, "GpuPrimitive must match std430 layout (32 bytes)");

    struct PrimitiveHandle
    {
        uint32_t primitive_id = 0xFFFFFFFFu;
        uint32_t index_offset = 0;
        uint32_t index_count = 0;
        int32_t  vertex_offset = 0;

        bool IsValid() const { return primitive_id != 0xFFFFFFFFu && index_count > 0; }
    };

    // Global vertex + index + primitive-descriptor store shared by raster and
    // (future) ray-tracing passes. Append-only in v1; re-uploads on growth.
    class GeometryStore
    {
    public:
        struct PrimitiveKey
        {
            uint64_t mesh_id = 0;
            uint32_t primitive_index = 0;

            bool operator==(const PrimitiveKey &other) const
            {
                return mesh_id == other.mesh_id && primitive_index == other.primitive_index;
            }
        };

        struct PrimitiveKeyHash
        {
            size_t operator()(const PrimitiveKey &key) const noexcept;
        };

        GeometryStore();

        // Creates the GL buffer objects. Must be called after the GL runtime
        // (GLAD) has been initialized and before any GetOrAppend / Sync calls.
        bool Init();

        // Appends geometry for `(mesh_id, primitive_index)` if not already
        // present. `material_index` is written into the primitive descriptor.
        // Returns true on success; `out_appended` indicates whether new data
        // was added (as opposed to a cache hit).
        bool GetOrAppend(uint64_t mesh_id,
                         uint32_t primitive_index,
                         const core::scene::MeshPrimitive &primitive,
                         uint32_t material_index,
                         PrimitiveHandle &out_handle,
                         bool &out_appended);

        // Rewrites material_index on an already-stored primitive. Cheap — no
        // geometry data is touched; primitive descriptor is flagged dirty.
        void SetPrimitiveMaterial(uint32_t primitive_id, uint32_t material_index);

        // Pushes any pending CPU changes to the GPU buffers. Returns true if
        // buffers are ready to consume.
        bool Sync();

        // Binds the shared VAO plus all owned SSBOs at their canonical
        // binding slots. Call after Sync().
        void BindForRaster() const;
        void BindSsbos() const;

        void Clear();

        const std::vector<GpuVertex>    &Vertices()   const { return m_vertices; }
        const std::vector<uint32_t>     &Indices()    const { return m_indices; }
        const std::vector<GpuPrimitive> &Primitives() const { return m_primitives; }

        uint32_t PrimitiveCount() const { return static_cast<uint32_t>(m_primitives.size()); }

    private:
        bool EnsureVaoCreated();

        std::vector<GpuVertex>    m_vertices;
        std::vector<uint32_t>     m_indices;
        std::vector<GpuPrimitive> m_primitives;

        std::unordered_map<PrimitiveKey, uint32_t, PrimitiveKeyHash> m_primitive_index;

        GLBuffer       m_vertex_buffer{};
        GLBuffer       m_index_buffer{};
        GLBuffer       m_primitive_buffer{};
        GLVertexArray  m_vao{};

        size_t m_vertex_capacity_bytes    = 0;
        size_t m_index_capacity_bytes     = 0;
        size_t m_primitive_capacity_bytes = 0;

        bool m_vao_configured   = false;
        bool m_vertices_dirty   = false;
        bool m_indices_dirty    = false;
        bool m_primitives_dirty = false;
    };

} // namespace hybrid::renderer
