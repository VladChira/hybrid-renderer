#include "renderer/passes/RayTracedPrimaryAlbedoPass.h"

#include "core/Profiling.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/raytracing/AccelerationStructureCache.h"
#include "renderer/stores/GeometryStore.h"
#include "renderer/stores/MaterialStore.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kWorkgroupSize = 8;
        constexpr GLuint kAlbedoImageBinding = 0;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    } // namespace

    RayTracedPrimaryAlbedoPass::RayTracedPrimaryAlbedoPass(GLShaderProgram *program,
                                                           GeometryStore *geometry_store,
                                                           MaterialStore *material_store,
                                                           raytracing::AccelerationStructureCache *as_cache)
        : m_program(program),
          m_geometry_store(geometry_store),
          m_material_store(material_store),
          m_as_cache(as_cache)
    {
    }

    bool RayTracedPrimaryAlbedoPass::Execute(const RayTracedPrimaryAlbedoPassInput &input)
    {
        HYBRID_PROFILE_ZONE_N("RayTracedPrimaryAlbedoPass::Execute");
        HYBRID_PROFILE_GL_ZONE("RayTracedPrimaryAlbedo");

        if (m_program == nullptr ||
            m_geometry_store == nullptr ||
            m_material_store == nullptr ||
            m_as_cache == nullptr ||
            input.settings == nullptr ||
            input.effective_view == nullptr ||
            input.output_texture == 0)
        {
            return false;
        }

        const RenderSettings &settings = *input.settings;
        const RenderView &view = *input.effective_view;
        const auto &extent = settings.render_extent;
        if (!extent.IsValid())
        {
            return false;
        }

        m_program->Use();

        glBindImageTexture(kAlbedoImageBinding,
                           input.output_texture,
                           0,
                           GL_FALSE,
                           0,
                           GL_WRITE_ONLY,
                           GL_RGBA8);

        m_geometry_store->BindSsbos();
        m_material_store->BindSsbo();
        m_as_cache->BindSsbos();

        m_program->SetUniformMat4("u_inv_view", glm::affineInverse(view.view));
        m_program->SetUniformMat4("u_inv_projection", glm::inverse(view.projection));
        m_program->SetUniformVec3("u_camera_position", view.position);
        m_program->SetUniform1ui("u_tlas_node_count", m_as_cache->Stats().tlas_nodes);
        m_program->SetUniformVec3("u_miss_color", glm::vec3(0.0f));

        const GLint output_size_loc = m_program->GetUniformLocation("u_output_size");
        if (output_size_loc >= 0)
        {
            glUniform2ui(output_size_loc,
                         static_cast<GLuint>(extent.width),
                         static_cast<GLuint>(extent.height));
        }

        const GLuint groups_x = CeilDiv(extent.width, kWorkgroupSize);
        const GLuint groups_y = CeilDiv(extent.height, kWorkgroupSize);
        glDispatchCompute(groups_x, groups_y, 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        GLShaderProgram::Unuse();
        return true;
    }

} // namespace hybrid::renderer
