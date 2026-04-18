#include "renderer/GeometryStore.h"

#include "renderer/ShaderBindings.h"

#include <algorithm>
#include <cstddef>

namespace hybrid::renderer
{

    namespace
    {
        constexpr size_t kMinVertexCapacityBytes    = 64 * 1024;
        constexpr size_t kMinIndexCapacityBytes     = 64 * 1024;
        constexpr size_t kMinPrimitiveCapacityBytes = 4  * 1024;

        // Grow the GPU allocation geometrically so we don't reallocate every
        // primitive, but never below the minimum "starter" size.
        size_t GrowCapacityBytes(size_t needed_bytes, size_t current_capacity_bytes, size_t minimum_bytes)
        {
            size_t target = std::max(current_capacity_bytes * 2, minimum_bytes);
            while (target < needed_bytes)
            {
                target *= 2;
            }
            return target;
        }
    } // namespace

    size_t GeometryStore::PrimitiveKeyHash::operator()(const PrimitiveKey &key) const noexcept
    {
        const size_t h1 = std::hash<uint64_t>{}(key.mesh_id);
        const size_t h2 = std::hash<uint32_t>{}(key.primitive_index);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }

    GeometryStore::GeometryStore() = default;

    bool GeometryStore::Init()
    {
        if (m_vertex_buffer.IsValid() && m_index_buffer.IsValid() && m_primitive_buffer.IsValid())
        {
            return true;
        }
        if (!m_vertex_buffer.Create(GL_ARRAY_BUFFER))           { return false; }
        if (!m_index_buffer.Create(GL_ELEMENT_ARRAY_BUFFER))    { return false; }
        if (!m_primitive_buffer.Create(GL_SHADER_STORAGE_BUFFER)) { return false; }
        return true;
    }

    bool GeometryStore::EnsureVaoCreated()
    {
        if (m_vao_configured)
        {
            return true;
        }
        if (!m_vertex_buffer.IsValid() || !m_index_buffer.IsValid() || !m_primitive_buffer.IsValid())
        {
            return false;
        }
        if (!m_vao.Create())
        {
            return false;
        }

        m_vao.Bind();
        m_vertex_buffer.Bind();
        m_index_buffer.Bind();

        m_vao.EnableAttrib(0);
        m_vao.SetAttribPointer(0, 3, GL_FLOAT, false, sizeof(GpuVertex), offsetof(GpuVertex, position));

        m_vao.EnableAttrib(1);
        m_vao.SetAttribPointer(1, 3, GL_FLOAT, false, sizeof(GpuVertex), offsetof(GpuVertex, normal));

        m_vao.EnableAttrib(2);
        m_vao.SetAttribPointer(2, 2, GL_FLOAT, false, sizeof(GpuVertex), offsetof(GpuVertex, uv0));

        m_vao.EnableAttrib(3);
        m_vao.SetAttribPointer(3, 2, GL_FLOAT, false, sizeof(GpuVertex), offsetof(GpuVertex, uv1));

        m_vao.EnableAttrib(4);
        m_vao.SetAttribPointer(4, 4, GL_FLOAT, false, sizeof(GpuVertex), offsetof(GpuVertex, tangent));

        GLVertexArray::Unbind();
        GLBuffer::Unbind(GL_ARRAY_BUFFER);
        GLBuffer::Unbind(GL_ELEMENT_ARRAY_BUFFER);

        m_vao_configured = true;
        return true;
    }

    bool GeometryStore::GetOrAppend(uint64_t mesh_id,
                                    uint32_t primitive_index,
                                    const core::scene::MeshPrimitive &primitive,
                                    uint32_t material_index,
                                    PrimitiveHandle &out_handle,
                                    bool &out_appended)
    {
        out_handle = {};
        out_appended = false;

        if (primitive.vertices.empty() || primitive.indices.empty())
        {
            return false;
        }

        PrimitiveKey key{};
        key.mesh_id = mesh_id;
        key.primitive_index = primitive_index;

        if (const auto it = m_primitive_index.find(key); it != m_primitive_index.end())
        {
            const uint32_t pid = it->second;
            const GpuPrimitive &desc = m_primitives[pid];

            if (desc.material_index != material_index)
            {
                SetPrimitiveMaterial(pid, material_index);
            }

            out_handle.primitive_id = pid;
            out_handle.index_offset = desc.index_offset;
            out_handle.index_count  = desc.index_count;
            out_handle.vertex_offset = static_cast<int32_t>(desc.vertex_offset);
            return true;
        }

        const uint32_t vertex_offset = static_cast<uint32_t>(m_vertices.size());
        const uint32_t index_offset  = static_cast<uint32_t>(m_indices.size());
        const uint32_t vertex_count  = static_cast<uint32_t>(primitive.vertices.size());
        const uint32_t index_count   = static_cast<uint32_t>(primitive.indices.size());

        m_vertices.reserve(m_vertices.size() + vertex_count);
        for (const core::scene::Vertex &v : primitive.vertices)
        {
            GpuVertex gv{};
            gv.position = v.position;
            gv.normal   = v.normal;
            gv.tangent  = v.tangent;
            gv.uv0      = v.uv0;
            gv.uv1      = v.uv1;
            gv.color0   = v.color0;
            m_vertices.push_back(gv);
        }

        m_indices.insert(m_indices.end(), primitive.indices.begin(), primitive.indices.end());

        GpuPrimitive desc{};
        desc.vertex_offset  = vertex_offset;
        desc.vertex_count   = vertex_count;
        desc.index_offset   = index_offset;
        desc.index_count    = index_count;
        desc.material_index = material_index;
        desc.blas_root      = 0;

        const uint32_t primitive_id = static_cast<uint32_t>(m_primitives.size());
        m_primitives.push_back(desc);
        m_primitive_index.emplace(key, primitive_id);

        m_vertices_dirty   = true;
        m_indices_dirty    = true;
        m_primitives_dirty = true;

        out_handle.primitive_id  = primitive_id;
        out_handle.index_offset  = index_offset;
        out_handle.index_count   = index_count;
        out_handle.vertex_offset = static_cast<int32_t>(vertex_offset);
        out_appended = true;
        return true;
    }

    void GeometryStore::SetPrimitiveMaterial(uint32_t primitive_id, uint32_t material_index)
    {
        if (primitive_id >= m_primitives.size())
        {
            return;
        }
        if (m_primitives[primitive_id].material_index == material_index)
        {
            return;
        }
        m_primitives[primitive_id].material_index = material_index;
        m_primitives_dirty = true;
    }

    void GeometryStore::SetPrimitiveBlas(uint32_t primitive_id, uint32_t blas_root, uint32_t blas_triangle_offset)
    {
        if (primitive_id >= m_primitives.size())
        {
            return;
        }
        GpuPrimitive &desc = m_primitives[primitive_id];
        if (desc.blas_root == blas_root && desc.blas_triangle_offset == blas_triangle_offset)
        {
            return;
        }
        desc.blas_root = blas_root;
        desc.blas_triangle_offset = blas_triangle_offset;
        m_primitives_dirty = true;
    }

    bool GeometryStore::FindPrimitiveId(uint64_t mesh_id, uint32_t primitive_index, uint32_t &out_primitive_id) const
    {
        PrimitiveKey key{};
        key.mesh_id = mesh_id;
        key.primitive_index = primitive_index;
        const auto it = m_primitive_index.find(key);
        if (it == m_primitive_index.end())
        {
            return false;
        }
        out_primitive_id = it->second;
        return true;
    }

    bool GeometryStore::Sync()
    {
        if (!m_vertex_buffer.IsValid() || !m_index_buffer.IsValid() || !m_primitive_buffer.IsValid())
        {
            return false;
        }
        if (!EnsureVaoCreated())
        {
            return false;
        }

        if (m_vertices_dirty && !m_vertices.empty())
        {
            const size_t needed_bytes = m_vertices.size() * sizeof(GpuVertex);
            m_vertex_buffer.Bind();
            if (needed_bytes > m_vertex_capacity_bytes)
            {
                m_vertex_capacity_bytes = GrowCapacityBytes(needed_bytes, m_vertex_capacity_bytes, kMinVertexCapacityBytes);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(m_vertex_capacity_bytes),
                             nullptr,
                             GL_STATIC_DRAW);
            }
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            static_cast<GLsizeiptr>(needed_bytes),
                            m_vertices.data());
            GLBuffer::Unbind(GL_ARRAY_BUFFER);
            m_vertices_dirty = false;
        }

        if (m_indices_dirty && !m_indices.empty())
        {
            const size_t needed_bytes = m_indices.size() * sizeof(uint32_t);
            m_index_buffer.Bind();
            if (needed_bytes > m_index_capacity_bytes)
            {
                m_index_capacity_bytes = GrowCapacityBytes(needed_bytes, m_index_capacity_bytes, kMinIndexCapacityBytes);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(m_index_capacity_bytes),
                             nullptr,
                             GL_STATIC_DRAW);
            }
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                            static_cast<GLsizeiptr>(needed_bytes),
                            m_indices.data());
            GLBuffer::Unbind(GL_ELEMENT_ARRAY_BUFFER);
            m_indices_dirty = false;
        }

        if (m_primitives_dirty && !m_primitives.empty())
        {
            const size_t needed_bytes = m_primitives.size() * sizeof(GpuPrimitive);
            m_primitive_buffer.Bind();
            if (needed_bytes > m_primitive_capacity_bytes)
            {
                m_primitive_capacity_bytes = GrowCapacityBytes(needed_bytes, m_primitive_capacity_bytes, kMinPrimitiveCapacityBytes);
                glBufferData(GL_SHADER_STORAGE_BUFFER,
                             static_cast<GLsizeiptr>(m_primitive_capacity_bytes),
                             nullptr,
                             GL_DYNAMIC_DRAW);
            }
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                            static_cast<GLsizeiptr>(needed_bytes),
                            m_primitives.data());
            GLBuffer::Unbind(GL_SHADER_STORAGE_BUFFER);
            m_primitives_dirty = false;
        }

        return true;
    }

    void GeometryStore::BindForRaster() const
    {
        if (m_vao.IsValid())
        {
            m_vao.Bind();
        }
        BindSsbos();
    }

    void GeometryStore::BindSsbos() const
    {
        if (m_vertex_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_geometry_vertices, m_vertex_buffer.Id());
        }
        if (m_index_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_geometry_indices, m_index_buffer.Id());
        }
        if (m_primitive_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_primitives, m_primitive_buffer.Id());
        }
    }

    void GeometryStore::Clear()
    {
        m_vertices.clear();
        m_indices.clear();
        m_primitives.clear();
        m_primitive_index.clear();

        m_vertex_capacity_bytes = 0;
        m_index_capacity_bytes = 0;
        m_primitive_capacity_bytes = 0;

        m_vertex_buffer.Destroy();
        m_index_buffer.Destroy();
        m_primitive_buffer.Destroy();
        m_vao.Destroy();

        m_vao_configured = false;
        m_vertices_dirty = false;
        m_indices_dirty = false;
        m_primitives_dirty = false;
    }

} // namespace hybrid::renderer
