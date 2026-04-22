#include "renderer/passes/DeferredLightingPass.h"

#include "core/Profiling.h"
#include "renderer/stores/LightStore.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLVertexArray.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer
{

    struct DeferredLightingPass::Impl
    {
        GLVertexArray fullscreen_vao{};
    };

    DeferredLightingPass::DeferredLightingPass(GLShaderProgram *deferred_shader, LightStore *light_store)
        : m_deferred_shader(deferred_shader),
          m_light_store(light_store),
          m_impl(std::make_unique<Impl>())
    {
    }

    DeferredLightingPass::~DeferredLightingPass() = default;

    const char *DeferredLightingPass::Name() const
    {
        return "DeferredLighting";
    }

    bool DeferredLightingPass::Execute(const DeferredLightingPassInput &input, DeferredLightingPassOutput &output)
    {
        HYBRID_PROFILE_ZONE_N("DeferredLightingPass::Execute");
        HYBRID_PROFILE_GL_ZONE("DeferredLightingPass");

        if (m_deferred_shader == nullptr ||
            m_light_store == nullptr ||
            m_impl == nullptr ||
            input.settings == nullptr ||
            input.scene_data == nullptr ||
            input.effective_view == nullptr ||
            input.scene_framebuffer_id == 0 ||
            input.scene_color == 0 ||
            input.gbuffer_rt0 == 0 ||
            input.gbuffer_rt1 == 0 ||
            input.gbuffer_depth == 0)
        {
            return false;
        }

        const RenderSettings &settings = *input.settings;
        const RenderView &effective_view = *input.effective_view;

        if (!m_impl->fullscreen_vao.IsValid() && !m_impl->fullscreen_vao.Create())
        {
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, input.scene_framebuffer_id);
        glViewport(0, 0,
                   static_cast<GLsizei>(settings.render_extent.width),
                   static_cast<GLsizei>(settings.render_extent.height));
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        m_deferred_shader->Use();
        m_deferred_shader->SetUniform1i("u_gbuffer_rt0", 0);
        m_deferred_shader->SetUniform1i("u_gbuffer_rt1", 1);
        m_deferred_shader->SetUniform1i("u_gbuffer_depth", 2);
        m_deferred_shader->SetUniform1i("u_skybox_cubemap", 3);
        m_deferred_shader->SetUniform1i("u_irradiance_cubemap", 4);
        m_deferred_shader->SetUniform1i("u_prefiltered_env_cubemap", 5);
        m_deferred_shader->SetUniform1i("u_brdf_lut", 6);
        m_deferred_shader->SetUniformMat4("u_inv_view", glm::affineInverse(effective_view.view));
        m_deferred_shader->SetUniformMat4("u_inv_projection", glm::inverse(effective_view.projection));
        m_deferred_shader->SetUniformVec3("u_camera_position", effective_view.position);
        m_deferred_shader->SetUniform1f("u_exposure", settings.exposure);
        m_deferred_shader->SetUniform1i("u_tonemapper", static_cast<int>(settings.tone_mapper));
        m_deferred_shader->SetUniform1f("u_legacy_curve_strength", settings.legacy_curve_strength);
        m_deferred_shader->SetUniform1f("u_legacy_gamma", settings.legacy_gamma);
        m_deferred_shader->SetUniform1f("u_aces_input_scale", settings.aces_input_scale);
        m_deferred_shader->SetUniform1f("u_aces_saturation", settings.aces_saturation);

        const bool has_skybox_cubemap = input.has_skybox && input.skybox_cubemap != 0;
        const bool has_irradiance_cubemap = input.has_skybox && input.convoluted_cubemap != 0;
        const bool has_specular_ibl = input.has_skybox &&
                                      input.prefiltered_cubemap != 0 &&
                                      input.brdf_lut != 0;
        m_deferred_shader->SetUniform1i("u_has_skybox", has_skybox_cubemap ? 1 : 0);
        m_deferred_shader->SetUniform1i("u_has_irradiance", has_irradiance_cubemap ? 1 : 0);
        m_deferred_shader->SetUniform1i("u_has_specular_ibl", has_specular_ibl ? 1 : 0);
        m_deferred_shader->SetUniform1f("u_skybox_intensity", input.skybox_intensity);
        m_deferred_shader->SetUniform1f("u_skybox_yaw_radians", input.skybox_yaw_radians);

        m_deferred_shader->SetUniform1ui("u_directional_light_count", m_light_store->DirectionalCount());
        m_deferred_shader->SetUniform1ui("u_point_light_count",       m_light_store->PointCount());
        m_deferred_shader->SetUniform1ui("u_area_light_count",        m_light_store->AreaCount());

        m_light_store->BindSsbos();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_CUBE_MAP, has_skybox_cubemap ? input.skybox_cubemap : 0);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_CUBE_MAP, has_irradiance_cubemap ? input.convoluted_cubemap : 0);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_CUBE_MAP, has_specular_ibl ? input.prefiltered_cubemap : 0);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, has_specular_ibl ? input.brdf_lut : 0);

        m_impl->fullscreen_vao.Bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDepthMask(GL_TRUE);

        output.color = input.scene_color;
        output.depth = input.gbuffer_depth;
        return true;
    }

} // namespace hybrid::renderer
