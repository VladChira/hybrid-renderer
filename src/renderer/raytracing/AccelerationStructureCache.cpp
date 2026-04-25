#include "renderer/raytracing/AccelerationStructureCache.h"

#include "core/Log.h"
#include "renderer/stores/GeometryStore.h"
#include "renderer/ShaderBindings.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer::raytracing
{

    namespace
    {
        constexpr size_t kMinBlasNodesCapacityBytes     = 64 * 1024;
        constexpr size_t kMinBlasTrianglesCapacityBytes = 64 * 1024;
        constexpr size_t kMinTlasNodesCapacityBytes     = 16 * 1024;
        constexpr size_t kMinTlasInstancesCapacityBytes = 16 * 1024;

        size_t GrowCapacityBytes(size_t needed_bytes, size_t current_capacity_bytes, size_t minimum_bytes)
        {
            size_t target = std::max(current_capacity_bytes * 2, minimum_bytes);
            while (target < needed_bytes)
            {
                target *= 2;
            }
            return target;
        }

        bool UploadSsbo(GLBuffer &buffer,
                        const void *data,
                        size_t needed_bytes,
                        size_t &capacity_bytes,
                        size_t minimum_bytes)
        {
            if (!buffer.IsValid())
            {
                return false;
            }
            buffer.Bind();
            if (needed_bytes == 0)
            {
                GLBuffer::Unbind(GL_SHADER_STORAGE_BUFFER);
                return true;
            }
            if (needed_bytes > capacity_bytes)
            {
                capacity_bytes = GrowCapacityBytes(needed_bytes, capacity_bytes, minimum_bytes);
                glBufferData(GL_SHADER_STORAGE_BUFFER,
                             static_cast<GLsizeiptr>(capacity_bytes),
                             nullptr,
                             GL_DYNAMIC_DRAW);
            }
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                            static_cast<GLsizeiptr>(needed_bytes),
                            data);
            GLBuffer::Unbind(GL_SHADER_STORAGE_BUFFER);
            return true;
        }

        core::scene::Aabb TransformAabb(const core::scene::Aabb &local, const glm::mat4 &world_from_local)
        {
            core::scene::Aabb out{};
            if (!local.valid)
            {
                return out;
            }
            // 8-corner transform. Conservative under non-uniform scale and
            // rotation — good enough for TLAS leaf bounds.
            const glm::vec3 corners[8] = {
                {local.min.x, local.min.y, local.min.z},
                {local.max.x, local.min.y, local.min.z},
                {local.min.x, local.max.y, local.min.z},
                {local.max.x, local.max.y, local.min.z},
                {local.min.x, local.min.y, local.max.z},
                {local.max.x, local.min.y, local.max.z},
                {local.min.x, local.max.y, local.max.z},
                {local.max.x, local.max.y, local.max.z},
            };
            glm::vec3 mn(std::numeric_limits<float>::infinity());
            glm::vec3 mx(-std::numeric_limits<float>::infinity());
            for (const glm::vec3 &c : corners)
            {
                const glm::vec3 w = glm::vec3(world_from_local * glm::vec4(c, 1.0f));
                mn = glm::min(mn, w);
                mx = glm::max(mx, w);
            }
            out.min = mn;
            out.max = mx;
            out.valid = true;
            return out;
        }
    } // namespace

    size_t AccelerationStructureCache::BlasKeyHash::operator()(const BlasKey &k) const noexcept
    {
        const size_t h1 = std::hash<uint64_t>{}(k.mesh_id);
        const size_t h2 = std::hash<uint32_t>{}(k.primitive_index);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }

    AccelerationStructureCache::AccelerationStructureCache() = default;

    bool AccelerationStructureCache::Init()
    {
        if (m_initialized)
        {
            return true;
        }
        if (!m_blas_nodes_buffer.Create(GL_SHADER_STORAGE_BUFFER))     { return false; }
        if (!m_blas_triangles_buffer.Create(GL_SHADER_STORAGE_BUFFER)) { return false; }
        if (!m_tlas_nodes_buffer.Create(GL_SHADER_STORAGE_BUFFER))     { return false; }
        if (!m_tlas_instances_buffer.Create(GL_SHADER_STORAGE_BUFFER)) { return false; }
        m_initialized = true;
        return true;
    }

    void AccelerationStructureCache::Reserve(size_t primitive_count)
    {
        m_blas_records.reserve(primitive_count);
    }

    bool AccelerationStructureCache::EnsurePrimitiveBlas(uint64_t mesh_id,
                                                        uint32_t primitive_index,
                                                        const core::scene::MeshPrimitive &primitive,
                                                        uint32_t primitive_id,
                                                        GeometryStore &geometry_store)
    {
        BlasKey key{};
        key.mesh_id = mesh_id;
        key.primitive_index = primitive_index;

        if (const auto it = m_blas_records.find(key); it != m_blas_records.end())
        {
            // Sanity: keep the GeometryStore primitive record in sync with
            // whatever it was patched to originally — defensive in case the
            // primitive id reordered across frames (it does not today).
            if (it->second.primitive_id != primitive_id)
            {
                geometry_store.SetPrimitiveBlas(primitive_id,
                                                it->second.node_offset,
                                                it->second.triangle_offset);
                it->second.primitive_id = primitive_id;
            }
            return true;
        }

        Blas blas = BuildBlas(primitive, BvhBuildConfig{});
        if (blas.nodes.empty() || blas.triangle_indices.empty())
        {
            return false;
        }

        BlasRecord record{};
        record.node_offset     = static_cast<uint32_t>(m_blas_nodes.size());
        record.node_count      = static_cast<uint32_t>(blas.nodes.size());
        record.triangle_offset = static_cast<uint32_t>(m_blas_triangles.size());
        record.triangle_count  = static_cast<uint32_t>(blas.triangle_indices.size());
        record.primitive_id    = primitive_id;
        record.stats           = blas.stats;
        record.bounds          = blas.bounds;

        // Append nodes with their child links rebased into global space.
        // Leaf nodes (right_or_count < 0) keep `left_or_first` — it addresses
        // the BLAS-local triangle-index table and the GPU shader re-adds
        // `primitive.blas_triangle_offset` at fetch time. Internal nodes use
        // `left_or_first` / `right_or_count` as node indices, which must be
        // rebased by `record.node_offset` so the concatenated global buffer
        // is self-consistent.
        const int32_t base = static_cast<int32_t>(record.node_offset);
        for (const BvhNode &local_node : blas.nodes)
        {
            BvhNode global_node = local_node;
            if (global_node.right_or_count >= 0)
            {
                global_node.left_or_first  += base;
                global_node.right_or_count += base;
            }
            m_blas_nodes.push_back(global_node);
        }

        m_blas_triangles.insert(m_blas_triangles.end(),
                                blas.triangle_indices.begin(),
                                blas.triangle_indices.end());

        geometry_store.SetPrimitiveBlas(primitive_id, record.node_offset, record.triangle_offset);

        m_blas_records.emplace(key, record);
        m_blas_dirty = true;
        m_stats.blas_count++;
        m_stats.blas_total_nodes     += record.node_count;
        m_stats.blas_total_leaves    += record.stats.leaf_count;
        m_stats.blas_total_triangles += record.stats.primitive_count;
        m_stats.blas_max_depth        = std::max(m_stats.blas_max_depth, record.stats.max_depth);
        m_stats.blas_build_ms_total  += record.stats.build_ms;
        return true;
    }

    void AccelerationStructureCache::SyncBlas(const FrameSceneData &scene, GeometryStore &geometry_store)
    {
        auto process_instance = [&](const RenderMeshInstance &instance)
        {
            const core::scene::MeshAsset *mesh = instance.mesh.Get();
            if (mesh == nullptr)
            {
                return;
            }
            for (size_t i = 0; i < mesh->primitives.size(); ++i)
            {
                const core::scene::MeshPrimitive &prim = mesh->primitives[i];
                uint32_t primitive_id = 0;
                if (!geometry_store.FindPrimitiveId(instance.mesh.Id().value,
                                                    static_cast<uint32_t>(i),
                                                    primitive_id))
                {
                    // Primitive has not been uploaded yet (GBufferPass runs
                    // after; on first frame this is normal — we'll catch it
                    // next frame).
                    continue;
                }
                EnsurePrimitiveBlas(instance.mesh.Id().value,
                                    static_cast<uint32_t>(i),
                                    prim,
                                    primitive_id,
                                    geometry_store);
            }
        };

        for (const auto &instance : scene.opaque_mesh_instances) { process_instance(instance); }
        for (const auto &instance : scene.masked_mesh_instances) { process_instance(instance); }
    }

    void AccelerationStructureCache::SyncTlas(const FrameSceneData &scene, const GeometryStore &geometry_store)
    {
        const auto start = std::chrono::steady_clock::now();

        std::vector<BvhInput> inputs;
        std::vector<GpuTlasInstance> cpu_instances;
        inputs.reserve(scene.opaque_mesh_instances.size() * 4 + scene.masked_mesh_instances.size() * 4);
        cpu_instances.reserve(inputs.capacity());

        auto add_instance = [&](const RenderMeshInstance &instance)
        {
            const core::scene::MeshAsset *mesh = instance.mesh.Get();
            if (mesh == nullptr)
            {
                return;
            }
            for (size_t i = 0; i < mesh->primitives.size(); ++i)
            {
                uint32_t primitive_id = 0;
                if (!geometry_store.FindPrimitiveId(instance.mesh.Id().value,
                                                    static_cast<uint32_t>(i),
                                                    primitive_id))
                {
                    continue;
                }
                BlasKey key{};
                key.mesh_id = instance.mesh.Id().value;
                key.primitive_index = static_cast<uint32_t>(i);
                const auto it = m_blas_records.find(key);
                if (it == m_blas_records.end())
                {
                    continue;
                }

                const core::scene::Aabb local_bounds = it->second.bounds;
                const core::scene::Aabb world_bounds = TransformAabb(local_bounds, instance.world_from_local);
                if (!world_bounds.valid)
                {
                    continue;
                }

                BvhInput input{};
                input.bounds   = world_bounds;
                input.centroid = 0.5f * (world_bounds.min + world_bounds.max);
                input.payload_index = static_cast<uint32_t>(cpu_instances.size());
                inputs.push_back(input);

                GpuTlasInstance gpu_instance{};
                gpu_instance.world_from_local = instance.world_from_local;
                gpu_instance.local_from_world = glm::affineInverse(instance.world_from_local);
                gpu_instance.primitive_id     = primitive_id;
                gpu_instance.entity_id        = static_cast<uint32_t>(instance.instance_id);
                cpu_instances.push_back(gpu_instance);
            }
        };

        for (const auto &instance : scene.opaque_mesh_instances) { add_instance(instance); }
        for (const auto &instance : scene.masked_mesh_instances) { add_instance(instance); }

        BvhBuildResult result = BuildBvh(inputs, BvhBuildConfig{});

        // Reorder the GPU instance table so leaves can address it directly —
        // leaves store `(first, count)` into `primitive_indices` which holds
        // payload indices that map back into cpu_instances. We rewrite the
        // TLAS instance buffer to be in leaf-visit order and update the leaf
        // ranges to address this ordered buffer.
        std::vector<GpuTlasInstance> ordered_instances;
        ordered_instances.reserve(cpu_instances.size());
        for (uint32_t payload : result.primitive_indices)
        {
            ordered_instances.push_back(cpu_instances[payload]);
        }

        m_tlas.nodes       = std::move(result.nodes);
        m_tlas.instances   = std::move(ordered_instances);
        m_tlas.bounds      = result.bounds;
        m_tlas.stats       = result.stats;

        const auto end = std::chrono::steady_clock::now();
        m_tlas.stats.build_ms = std::chrono::duration<double, std::milli>(end - start).count();

        m_stats.tlas_nodes     = static_cast<uint32_t>(m_tlas.nodes.size());
        m_stats.tlas_leaves    = m_tlas.stats.leaf_count;
        m_stats.tlas_max_depth = m_tlas.stats.max_depth;
        m_stats.tlas_instances = static_cast<uint32_t>(m_tlas.instances.size());
        m_stats.tlas_build_ms  = m_tlas.stats.build_ms;

        m_tlas_dirty = true;
    }

    bool AccelerationStructureCache::Upload()
    {
        if (!m_initialized)
        {
            return false;
        }

        if (m_blas_dirty)
        {
            UploadSsbo(m_blas_nodes_buffer,
                       m_blas_nodes.data(),
                       m_blas_nodes.size() * sizeof(BvhNode),
                       m_gpu_blas_nodes_capacity,
                       kMinBlasNodesCapacityBytes);
            UploadSsbo(m_blas_triangles_buffer,
                       m_blas_triangles.data(),
                       m_blas_triangles.size() * sizeof(uint32_t),
                       m_gpu_blas_triangles_capacity,
                       kMinBlasTrianglesCapacityBytes);
            m_blas_dirty = false;
        }

        if (m_tlas_dirty)
        {
            UploadSsbo(m_tlas_nodes_buffer,
                       m_tlas.nodes.data(),
                       m_tlas.nodes.size() * sizeof(BvhNode),
                       m_gpu_tlas_nodes_capacity,
                       kMinTlasNodesCapacityBytes);
            UploadSsbo(m_tlas_instances_buffer,
                       m_tlas.instances.data(),
                       m_tlas.instances.size() * sizeof(GpuTlasInstance),
                       m_gpu_tlas_instances_capacity,
                       kMinTlasInstancesCapacityBytes);
            m_tlas_dirty = false;
        }

        m_stats.gpu_blas_nodes_bytes     = m_blas_nodes.size()     * sizeof(BvhNode);
        m_stats.gpu_blas_triangles_bytes = m_blas_triangles.size() * sizeof(uint32_t);
        m_stats.gpu_tlas_nodes_bytes     = m_tlas.nodes.size()     * sizeof(BvhNode);
        m_stats.gpu_tlas_instances_bytes = m_tlas.instances.size() * sizeof(GpuTlasInstance);
        return true;
    }

    void AccelerationStructureCache::BindSsbos() const
    {
        if (m_blas_nodes_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_blas_nodes, m_blas_nodes_buffer.Id());
        }
        if (m_blas_triangles_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_blas_triangles, m_blas_triangles_buffer.Id());
        }
        if (m_tlas_nodes_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_tlas_nodes, m_tlas_nodes_buffer.Id());
        }
        if (m_tlas_instances_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_tlas_instances, m_tlas_instances_buffer.Id());
        }
    }

    void AccelerationStructureCache::Clear()
    {
        m_blas_records.clear();
        m_blas_nodes.clear();
        m_blas_triangles.clear();
        m_tlas = Tlas{};

        m_blas_nodes_buffer.Destroy();
        m_blas_triangles_buffer.Destroy();
        m_tlas_nodes_buffer.Destroy();
        m_tlas_instances_buffer.Destroy();

        m_gpu_blas_nodes_capacity = 0;
        m_gpu_blas_triangles_capacity = 0;
        m_gpu_tlas_nodes_capacity = 0;
        m_gpu_tlas_instances_capacity = 0;

        m_blas_dirty = false;
        m_tlas_dirty = false;
        m_initialized = false;
        m_stats = {};
    }

} // namespace hybrid::renderer::raytracing
