#include "renderer/passes/RayTracedShadowPass.h"

#include "core/Profiling.h"
#include "renderer/GeometryStore.h"
#include "renderer/LightStore.h"
#include "renderer/raytracing/AccelerationStructureCache.h"
#include "renderer/opengl/GLShaderProgram.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kWorkgroupSize       = 8;
        constexpr GLuint kShadowImageBinding  = 0;
        constexpr GLuint kGbufferRt1TexUnit   = 0;
        constexpr GLuint kGbufferDepthTexUnit = 1;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    } // namespace

    RayTracedShadowPass::RayTracedShadowPass(GLShaderProgram *program,
                                             GeometryStore *geometry_store,
                                             raytracing::AccelerationStructureCache *as_cache)
        : m_program(program),
          m_geometry_store(geometry_store),
          m_as_cache(as_cache)
    {
    }

    bool RayTracedShadowPass::Execute(const RayTracedShadowPassInput &input)
    {
        HYBRID_PROFILE_ZONE_N("RayTracedShadowPass::Execute");
        HYBRID_PROFILE_GL_ZONE("RayTracedShadowPass");

        if (m_program == nullptr ||
            m_geometry_store == nullptr ||
            m_as_cache == nullptr ||
            input.settings == nullptr ||
            input.effective_view == nullptr ||
            input.light_store == nullptr ||
            input.gbuffer_depth == 0 ||
            input.gbuffer_rt1 == 0 ||
            input.shadow_mask_array == 0)
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

        if (!settings.enable_raytrace_shadows)
        {
            return true;
        }

        const auto &casters = input.light_store->ShadowCasters();
        if (casters.empty())
        {
            return true;
        }

        m_program->Use();

        m_geometry_store->BindSsbos();
        m_as_cache->BindSsbos();

        glActiveTexture(GL_TEXTURE0 + kGbufferRt1TexUnit);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt1);
        glActiveTexture(GL_TEXTURE0 + kGbufferDepthTexUnit);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);
        m_program->SetUniform1i("u_gbuffer_rt1",   static_cast<GLint>(kGbufferRt1TexUnit));
        m_program->SetUniform1i("u_gbuffer_depth", static_cast<GLint>(kGbufferDepthTexUnit));

        m_program->SetUniformMat4("u_inv_view",       glm::affineInverse(view.view));
        m_program->SetUniformMat4("u_inv_projection", glm::inverse(view.projection));
        m_program->SetUniformVec3("u_camera_position", view.position);

        m_program->SetUniform1ui("u_tlas_node_count", m_as_cache->Stats().tlas_nodes);
        m_program->SetUniform1ui("u_frame_index",     input.frame_index);
        m_program->SetUniform1f("u_normal_bias",      settings.raytrace_shadow_normal_bias);

        const GLint output_size_loc = m_program->GetUniformLocation("u_output_size");
        if (output_size_loc >= 0)
        {
            glUniform2ui(output_size_loc,
                         static_cast<GLuint>(extent.width),
                         static_cast<GLuint>(extent.height));
        }

        const GLuint groups_x = CeilDiv(extent.width, kWorkgroupSize);
        const GLuint groups_y = CeilDiv(extent.height, kWorkgroupSize);

        for (const ShadowCaster &caster : casters)
        {
            glBindImageTexture(kShadowImageBinding,
                               input.shadow_mask_array,
                               0,
                               GL_FALSE,
                               static_cast<GLint>(caster.layer),
                               GL_WRITE_ONLY,
                               GL_R8);

            m_program->SetUniform1ui("u_light_type", static_cast<uint32_t>(caster.type));
            m_program->SetUniformVec3("u_light_direction", caster.direction);
            m_program->SetUniformVec3("u_light_position",  caster.position);
            const GLint size_loc = m_program->GetUniformLocation("u_light_size");
            if (size_loc >= 0)
            {
                glUniform2f(size_loc, caster.size.x, caster.size.y);
            }

            glDispatchCompute(groups_x, groups_y, 1);
        }

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glActiveTexture(GL_TEXTURE0 + kGbufferDepthTexUnit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + kGbufferRt1TexUnit);
        glBindTexture(GL_TEXTURE_2D, 0);

        GLShaderProgram::Unuse();
        return true;
    }

} // namespace hybrid::renderer
