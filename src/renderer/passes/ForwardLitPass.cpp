#include "renderer/passes/ForwardLitPass.h"

#include "renderer/opengl/GLBuffer.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLVertexArray.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace hybrid::renderer
{

    namespace
    {
        struct PrimitiveCacheKey
        {
            uint64_t mesh_id = 0;
            uint32_t primitive_index = 0;

            bool operator==(const PrimitiveCacheKey &other) const
            {
                return mesh_id == other.mesh_id && primitive_index == other.primitive_index;
            }
        };

        struct PrimitiveCacheKeyHash
        {
            size_t operator()(const PrimitiveCacheKey &key) const noexcept
            {
                size_t h1 = std::hash<uint64_t>{}(key.mesh_id);
                size_t h2 = std::hash<uint32_t>{}(key.primitive_index);
                return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
            }
        };

        struct CachedPrimitiveGpu
        {
            GLVertexArray vao{};
            GLBuffer vertex_buffer{GL_ARRAY_BUFFER};
            GLBuffer index_buffer{GL_ELEMENT_ARRAY_BUFFER};
            GLsizei index_count = 0;
        };

        template <typename Fn>
        void ForEachMeshInstance(const FrameSceneData &scene, Fn &&fn)
        {
            for (const auto &instance : scene.opaque_mesh_instances)
            {
                fn(instance);
            }

            for (const auto &instance : scene.masked_mesh_instances)
            {
                fn(instance);
            }

            for (const auto &instance : scene.blended_mesh_instances)
            {
                fn(instance);
            }
        }

        glm::vec3 ResolvePrimitiveBaseColor(const core::scene::MeshPrimitive &primitive)
        {
            if (const auto *material = primitive.material.Get())
            {
                return glm::vec3(material->base_color_factor);
            }
            return glm::vec3(0.8f);
        }

        bool UploadPrimitiveToGpu(const core::scene::MeshPrimitive &primitive, CachedPrimitiveGpu &out_gpu)
        {
            if (primitive.vertices.empty() || primitive.indices.empty())
            {
                return false;
            }

            if (!out_gpu.vao.Create())
            {
                return false;
            }
            if (!out_gpu.vertex_buffer.IsValid() || !out_gpu.index_buffer.IsValid())
            {
                return false;
            }

            out_gpu.vao.Bind();

            out_gpu.vertex_buffer.Bind();
            out_gpu.vertex_buffer.SetData(
                static_cast<GLsizeiptr>(primitive.vertices.size() * sizeof(core::scene::Vertex)),
                primitive.vertices.data(),
                GL_STATIC_DRAW);

            out_gpu.index_buffer.Bind();
            out_gpu.index_buffer.SetData(
                static_cast<GLsizeiptr>(primitive.indices.size() * sizeof(uint32_t)),
                primitive.indices.data(),
                GL_STATIC_DRAW);

            out_gpu.vao.EnableAttrib(0);
            out_gpu.vao.SetAttribPointer(
                0, 3, GL_FLOAT, false, sizeof(core::scene::Vertex), offsetof(core::scene::Vertex, position));

            out_gpu.vao.EnableAttrib(1);
            out_gpu.vao.SetAttribPointer(
                1, 3, GL_FLOAT, false, sizeof(core::scene::Vertex), offsetof(core::scene::Vertex, normal));

            out_gpu.vao.EnableAttrib(2);
            out_gpu.vao.SetAttribPointer(
                2, 2, GL_FLOAT, false, sizeof(core::scene::Vertex), offsetof(core::scene::Vertex, uv0));

            GLVertexArray::Unbind();
            GLBuffer::Unbind(GL_ARRAY_BUFFER);
            GLBuffer::Unbind(GL_ELEMENT_ARRAY_BUFFER);

            out_gpu.index_count = static_cast<GLsizei>(primitive.indices.size());
            return true;
        }
    } // namespace

    struct ForwardLitPass::Impl
    {
        std::unordered_map<PrimitiveCacheKey, CachedPrimitiveGpu, PrimitiveCacheKeyHash> primitive_cache;
    };

    ForwardLitPass::ForwardLitPass(GLShaderProgram *forward_shader)
        : m_forward_shader(forward_shader),
          m_impl(std::make_unique<Impl>())
    {
    }

    ForwardLitPass::~ForwardLitPass() = default;

    const char *ForwardLitPass::Name() const
    {
        return "ForwardLit";
    }

    bool ForwardLitPass::Execute(PassContext &context)
    {
        if (m_forward_shader == nullptr ||
            m_impl == nullptr ||
            context.scene_data == nullptr ||
            context.effective_view == nullptr ||
            context.inputs.settings == nullptr ||
            context.stats == nullptr ||
            context.outputs == nullptr)
        {
            return false;
        }

        const FrameSceneData &scene = *context.scene_data;
        const RenderView &effective_view = *context.effective_view;
        const RenderSettings &settings = *context.inputs.settings;

        m_forward_shader->Use();
        m_forward_shader->SetUniformMat4("u_view", effective_view.view);
        m_forward_shader->SetUniformMat4("u_projection", effective_view.projection);
        m_forward_shader->SetUniformVec3("u_camera_position", effective_view.position);
        m_forward_shader->SetUniform1i("u_render_mode", static_cast<int>(settings.mode));

        const bool wireframe = settings.mode == RenderMode::Wireframe;
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

        auto draw_instance = [&](const RenderMeshInstance &instance)
        {
            const core::scene::MeshAsset *mesh = instance.mesh.Get();
            if (mesh == nullptr)
            {
                return;
            }

            for (size_t primitive_index = 0; primitive_index < mesh->primitives.size(); ++primitive_index)
            {
                const core::scene::MeshPrimitive &primitive = mesh->primitives[primitive_index];
                context.stats->submitted_primitives++;
                context.stats->submitted_vertices += primitive.vertices.size();
                context.stats->submitted_triangles += primitive.indices.size() / 3;

                PrimitiveCacheKey key{};
                key.mesh_id = instance.mesh.Id().value;
                key.primitive_index = static_cast<uint32_t>(primitive_index);

                auto gpu_it = m_impl->primitive_cache.find(key);
                if (gpu_it == m_impl->primitive_cache.end())
                {
                    CachedPrimitiveGpu cached_primitive{};
                    if (!UploadPrimitiveToGpu(primitive, cached_primitive))
                    {
                        continue;
                    }
                    gpu_it = m_impl->primitive_cache.emplace(key, std::move(cached_primitive)).first;
                }

                CachedPrimitiveGpu &gpu = gpu_it->second;
                if (gpu.index_count == 0)
                {
                    continue;
                }

                m_forward_shader->SetUniformMat4("u_model", instance.world_from_local);
                m_forward_shader->SetUniformVec3("u_base_color", ResolvePrimitiveBaseColor(primitive));

                gpu.vao.Bind();
                glDrawElements(GL_TRIANGLES, gpu.index_count, GL_UNSIGNED_INT, nullptr);
            }
        };

        ForEachMeshInstance(scene, draw_instance);

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        context.outputs->color = context.targets.scene_color;
        context.outputs->depth = context.targets.scene_depth;
        return true;
    }

} // namespace hybrid::renderer
