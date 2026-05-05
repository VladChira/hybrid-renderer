#include "renderer/passes/RayTracedReflectionPass.h"

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
        constexpr GLuint kReflectionImageBinding = 0;
        constexpr GLuint kGbufferRt0TexUnit = 0;
        constexpr GLuint kGbufferRt1TexUnit = 1;
        constexpr GLuint kGbufferDepthTexUnit = 2;
        constexpr GLuint kSkyboxTexUnit = 3;
        constexpr GLuint kIrradianceTexUnit = 4;
        constexpr GLuint kPrefilteredTexUnit = 5;
        constexpr GLuint kBrdfLutTexUnit = 6;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    } // namespace

    RayTracedReflectionPass::RayTracedReflectionPass(GLShaderProgram *program,
                                                     GeometryStore *geometry_store,
                                                     MaterialStore *material_store,
                                                     raytracing::AccelerationStructureCache *as_cache)
        : m_program(program),
          m_geometry_store(geometry_store),
          m_material_store(material_store),
          m_as_cache(as_cache)
    {
    }

    bool RayTracedReflectionPass::Execute(const RayTracedReflectionPassInput &input)
    {
        HYBRID_PROFILE_ZONE_N("RayTracedReflectionPass::Execute");
        HYBRID_PROFILE_GL_ZONE("RayTracedReflection");

        if (m_program == nullptr ||
            m_geometry_store == nullptr ||
            m_material_store == nullptr ||
            m_as_cache == nullptr ||
            input.settings == nullptr ||
            input.effective_view == nullptr ||
            input.gbuffer_rt0 == 0 ||
            input.gbuffer_rt1 == 0 ||
            input.gbuffer_depth == 0 ||
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

        const bool has_skybox_cubemap = input.has_skybox && input.skybox_cubemap != 0;
        const bool has_irradiance_cubemap = input.has_skybox && input.convoluted_cubemap != 0;
        const bool has_prefiltered_env_cubemap = input.has_skybox && input.prefiltered_cubemap != 0;
        const bool has_specular_ibl = input.has_skybox &&
                                      input.prefiltered_cubemap != 0 &&
                                      input.brdf_lut != 0;

        m_program->Use();

        glBindImageTexture(kReflectionImageBinding,
                           input.output_texture,
                           0,
                           GL_FALSE,
                           0,
                           GL_WRITE_ONLY,
                           GL_RGBA16F);

        m_geometry_store->BindSsbos();
        m_material_store->BindSsbo();
        m_as_cache->BindSsbos();

        glActiveTexture(GL_TEXTURE0 + kGbufferRt0TexUnit);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt0);
        glActiveTexture(GL_TEXTURE0 + kGbufferRt1TexUnit);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt1);
        glActiveTexture(GL_TEXTURE0 + kGbufferDepthTexUnit);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);
        glActiveTexture(GL_TEXTURE0 + kSkyboxTexUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, has_skybox_cubemap ? input.skybox_cubemap : 0);
        glActiveTexture(GL_TEXTURE0 + kIrradianceTexUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, has_irradiance_cubemap ? input.convoluted_cubemap : 0);
        glActiveTexture(GL_TEXTURE0 + kPrefilteredTexUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, has_prefiltered_env_cubemap ? input.prefiltered_cubemap : 0);
        glActiveTexture(GL_TEXTURE0 + kBrdfLutTexUnit);
        glBindTexture(GL_TEXTURE_2D, has_specular_ibl ? input.brdf_lut : 0);

        m_program->SetUniform1i("u_gbuffer_rt0", static_cast<GLint>(kGbufferRt0TexUnit));
        m_program->SetUniform1i("u_gbuffer_rt1", static_cast<GLint>(kGbufferRt1TexUnit));
        m_program->SetUniform1i("u_gbuffer_depth", static_cast<GLint>(kGbufferDepthTexUnit));
        m_program->SetUniform1i("u_skybox_cubemap", static_cast<GLint>(kSkyboxTexUnit));
        m_program->SetUniform1i("u_irradiance_cubemap", static_cast<GLint>(kIrradianceTexUnit));
        m_program->SetUniform1i("u_prefiltered_env_cubemap", static_cast<GLint>(kPrefilteredTexUnit));
        m_program->SetUniform1i("u_brdf_lut", static_cast<GLint>(kBrdfLutTexUnit));

        m_program->SetUniformMat4("u_inv_view", glm::affineInverse(view.view));
        m_program->SetUniformMat4("u_inv_projection", glm::inverse(view.projection));
        m_program->SetUniformVec3("u_camera_position", view.position);
        m_program->SetUniform1ui("u_tlas_node_count", m_as_cache->Stats().tlas_nodes);
        m_program->SetUniform1ui("u_frame_index", input.frame_index);
        m_program->SetUniform1f("u_normal_bias", settings.raytrace_reflection_normal_bias);

        const GLint output_size_loc = m_program->GetUniformLocation("u_output_size");
        if (output_size_loc >= 0)
        {
            glUniform2ui(output_size_loc,
                         static_cast<GLuint>(extent.width),
                         static_cast<GLuint>(extent.height));
        }

        m_program->SetUniform1i("u_has_skybox", has_skybox_cubemap ? 1 : 0);
        m_program->SetUniform1i("u_has_irradiance", has_irradiance_cubemap ? 1 : 0);
        m_program->SetUniform1i("u_has_prefiltered_env", has_prefiltered_env_cubemap ? 1 : 0);
        m_program->SetUniform1i("u_has_specular_ibl", has_specular_ibl ? 1 : 0);
        m_program->SetUniform1f("u_skybox_intensity", input.skybox_intensity);
        m_program->SetUniform1f("u_skybox_yaw_radians", input.skybox_yaw_radians);

        const GLuint groups_x = CeilDiv(extent.width, kWorkgroupSize);
        const GLuint groups_y = CeilDiv(extent.height, kWorkgroupSize);
        glDispatchCompute(groups_x, groups_y, 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glActiveTexture(GL_TEXTURE0 + kBrdfLutTexUnit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + kPrefilteredTexUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glActiveTexture(GL_TEXTURE0 + kIrradianceTexUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glActiveTexture(GL_TEXTURE0 + kSkyboxTexUnit);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glActiveTexture(GL_TEXTURE0 + kGbufferDepthTexUnit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + kGbufferRt1TexUnit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + kGbufferRt0TexUnit);
        glBindTexture(GL_TEXTURE_2D, 0);

        GLShaderProgram::Unuse();
        return true;
    }

} // namespace hybrid::renderer
