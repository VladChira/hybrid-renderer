#include "renderer/passes/RayTracedShadowPass.h"

#include "core/Profiling.h"
#include "renderer/FrameResources.h"
#include "renderer/stores/GeometryStore.h"
#include "renderer/stores/LightStore.h"
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
        constexpr uint32_t kLightTypeEnvironment = 3u;

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
        const auto &gbuffer_extent = settings.render_extent;
        const RenderExtent shadow_extent =
            input.shadow_extent.IsValid() ? input.shadow_extent : gbuffer_extent;
        if (!gbuffer_extent.IsValid() || !shadow_extent.IsValid())
        {
            return false;
        }

        const auto &casters = input.light_store->ShadowCasters();
        const bool trace_light_shadows = settings.enable_ray_traced_shadows;
        const bool trace_environment_visibility = settings.enable_ray_traced_hdri_visibility;
        if ((!trace_light_shadows || casters.empty()) && !trace_environment_visibility)
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
                         static_cast<GLuint>(shadow_extent.width),
                         static_cast<GLuint>(shadow_extent.height));
        }

        const GLint gbuffer_size_loc = m_program->GetUniformLocation("u_gbuffer_size");
        if (gbuffer_size_loc >= 0)
        {
            glUniform2ui(gbuffer_size_loc,
                         static_cast<GLuint>(gbuffer_extent.width),
                         static_cast<GLuint>(gbuffer_extent.height));
        }

        const GLuint groups_x = CeilDiv(shadow_extent.width, kWorkgroupSize);
        const GLuint groups_y = CeilDiv(shadow_extent.height, kWorkgroupSize);

        auto dispatch_shadow_layer = [&](uint32_t layer,
                                         uint32_t light_type,
                                         const glm::vec3 &light_direction,
                                         const glm::vec3 &light_position,
                                         const glm::vec2 &light_size)
        {
            glBindImageTexture(kShadowImageBinding,
                               input.shadow_mask_array,
                               0,
                               GL_FALSE,
                               static_cast<GLint>(layer),
                               GL_WRITE_ONLY,
                               GL_R8);

            m_program->SetUniform1ui("u_light_type", light_type);
            m_program->SetUniformVec3("u_light_direction", light_direction);
            m_program->SetUniformVec3("u_light_position", light_position);
            const GLint size_loc = m_program->GetUniformLocation("u_light_size");
            if (size_loc >= 0)
            {
                glUniform2f(size_loc, light_size.x, light_size.y);
            }

            glDispatchCompute(groups_x, groups_y, 1);
        };

        if (trace_light_shadows)
        {
            for (const ShadowCaster &caster : casters)
            {
                dispatch_shadow_layer(caster.layer,
                                      static_cast<uint32_t>(caster.type),
                                      caster.direction,
                                      caster.position,
                                      caster.size);
            }
        }

        if (trace_environment_visibility)
        {
            dispatch_shadow_layer(kRaytraceEnvironmentShadowLayer,
                                  kLightTypeEnvironment,
                                  glm::vec3(0.0f),
                                  glm::vec3(0.0f),
                                  glm::vec2(0.0f));
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
