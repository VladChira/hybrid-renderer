#include "renderer/passes/GBufferPass.h"

#include "core/Profiling.h"
#include "renderer/GeometryStore.h"
#include "renderer/MaterialStore.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLVertexArray.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace hybrid::renderer
{

    GBufferPass::GBufferPass(GLShaderProgram *gbuffer_shader,
                             GeometryStore *geometry_store,
                             MaterialStore *material_store)
        : m_gbuffer_shader(gbuffer_shader),
          m_geometry_store(geometry_store),
          m_material_store(material_store)
    {
    }

    GBufferPass::~GBufferPass() = default;

    const char *GBufferPass::Name() const
    {
        return "GBuffer";
    }

    bool GBufferPass::Execute(const GBufferPassInput &input, GBufferPassOutput &output)
    {
        HYBRID_PROFILE_ZONE_N("GBufferPass::Execute");
        HYBRID_PROFILE_GL_ZONE("GBufferPass");

        if (m_gbuffer_shader == nullptr ||
            m_geometry_store == nullptr ||
            m_material_store == nullptr ||
            input.scene_data == nullptr ||
            input.effective_view == nullptr ||
            input.settings == nullptr ||
            input.gbuffer_framebuffer_id == 0)
        {
            return false;
        }

        const FrameSceneData &scene = *input.scene_data;
        const RenderView &effective_view = *input.effective_view;
        const RenderSettings &settings = *input.settings;
        RendererStats::GBufferStats *gbuffer_stats =
            input.renderer_stats != nullptr ? &input.renderer_stats->gbuffer : nullptr;

        // Resolve primitives up front: material index + geometry handle.
        // Doing this before binding GPU state lets us batch GeometryStore and
        // MaterialStore syncs into single uploads.
        struct PrimitiveDraw
        {
            const RenderMeshInstance *instance;
            PrimitiveHandle          handle;
            uint32_t                 material_index;
        };

        std::vector<PrimitiveDraw> draws;

        auto prepare_instance = [&](const RenderMeshInstance &instance)
        {
            const core::scene::MeshAsset *mesh = instance.mesh.Get();
            if (mesh == nullptr)
            {
                return;
            }

            for (size_t primitive_index = 0; primitive_index < mesh->primitives.size(); ++primitive_index)
            {
                const core::scene::MeshPrimitive &primitive = mesh->primitives[primitive_index];
                const uint32_t material_index = m_material_store->GetOrUploadMaterial(primitive.material);

                PrimitiveHandle handle{};
                bool appended = false;
                if (!m_geometry_store->GetOrAppend(instance.mesh.Id().value,
                                                   static_cast<uint32_t>(primitive_index),
                                                   primitive,
                                                   material_index,
                                                   handle,
                                                   appended))
                {
                    continue;
                }
                if (gbuffer_stats != nullptr && appended)
                {
                    gbuffer_stats->primitive_cache_misses++;
                    gbuffer_stats->primitive_uploads++;
                }
                if (!handle.IsValid())
                {
                    continue;
                }

                draws.push_back(PrimitiveDraw{&instance, handle, material_index});
            }
        };

        for (const auto &instance : scene.opaque_mesh_instances) { prepare_instance(instance); }
        for (const auto &instance : scene.masked_mesh_instances) { prepare_instance(instance); }

        m_geometry_store->Sync();
        m_material_store->Sync();

        {
            HYBRID_PROFILE_ZONE_N("GBufferPass::Setup");
            glBindFramebuffer(GL_FRAMEBUFFER, input.gbuffer_framebuffer_id);
            glViewport(0, 0,
                       static_cast<GLsizei>(settings.render_extent.width),
                       static_cast<GLsizei>(settings.render_extent.height));
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

            const GLfloat clear_rt0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            const GLfloat clear_rt1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            const GLuint clear_entity_id[1] = {std::numeric_limits<uint32_t>::max()};
            glClearBufferfv(GL_COLOR, 0, clear_rt0);
            glClearBufferfv(GL_COLOR, 1, clear_rt1);
            glClearBufferuiv(GL_COLOR, 2, clear_entity_id);
            glClear(GL_DEPTH_BUFFER_BIT);

            m_gbuffer_shader->Use();
            m_gbuffer_shader->SetUniformMat4("u_view", effective_view.view);
            m_gbuffer_shader->SetUniformMat4("u_projection", effective_view.projection);
        }

        m_geometry_store->BindForRaster();
        m_material_store->BindSsbo();

        for (const PrimitiveDraw &draw : draws)
        {
            m_gbuffer_shader->SetUniformMat4("u_model", draw.instance->world_from_local);
            m_gbuffer_shader->SetUniform1ui("u_material_index", draw.material_index);
            m_gbuffer_shader->SetUniform1ui("u_instance_id", static_cast<uint32_t>(draw.instance->instance_id));
            if (gbuffer_stats != nullptr)
            {
                gbuffer_stats->uniform_updates += 3;
            }

            const GLsizeiptr index_byte_offset =
                static_cast<GLsizeiptr>(draw.handle.index_offset) * static_cast<GLsizeiptr>(sizeof(uint32_t));
            glDrawElementsBaseVertex(GL_TRIANGLES,
                                     static_cast<GLsizei>(draw.handle.index_count),
                                     GL_UNSIGNED_INT,
                                     reinterpret_cast<const void *>(index_byte_offset),
                                     draw.handle.vertex_offset);
            if (gbuffer_stats != nullptr)
            {
                gbuffer_stats->draw_calls++;
            }
        }

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        output.gbuffer_rt0 = input.gbuffer_rt0;
        output.gbuffer_rt1 = input.gbuffer_rt1;
        output.gbuffer_entity_id = input.gbuffer_entity_id;
        output.depth = input.gbuffer_depth;
        return true;
    }

} // namespace hybrid::renderer
