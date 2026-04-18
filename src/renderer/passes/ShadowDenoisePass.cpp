#include "renderer/passes/ShadowDenoisePass.h"

#include "core/Profiling.h"
#include "renderer/FrameResources.h"
#include "renderer/LightStore.h"
#include "renderer/opengl/GLShaderProgram.h"

#include <algorithm>

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kWorkgroupSize = 8;
        constexpr GLuint kImageBinding  = 0;
        constexpr GLuint kTexUnitMaskIn = 0;
        constexpr GLuint kTexUnitHistory = 1;
        constexpr GLuint kTexUnitDepth = 2;
        constexpr GLuint kTexUnitNormal = 3;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }

        void BindArrayTextureUnit(GLuint unit, GLuint tex_id)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D_ARRAY, tex_id);
        }

        void BindTexture2DUnit(GLuint unit, GLuint tex_id)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, tex_id);
        }
    } // namespace

    ShadowDenoisePass::ShadowDenoisePass(GLShaderProgram *temporal_program,
                                         GLShaderProgram *atrous_program)
        : m_temporal_program(temporal_program),
          m_atrous_program(atrous_program)
    {
    }

    bool ShadowDenoisePass::Execute(const ShadowDenoisePassInput &input, ShadowDenoisePassOutput &output)
    {
        HYBRID_PROFILE_ZONE_N("ShadowDenoisePass::Execute");
        HYBRID_PROFILE_GL_ZONE("ShadowDenoisePass");

        output.filtered_mask_array = 0;

        if (m_temporal_program == nullptr ||
            m_atrous_program == nullptr ||
            input.settings == nullptr ||
            input.effective_view == nullptr ||
            input.light_store == nullptr ||
            input.gbuffer_depth == 0 ||
            input.gbuffer_rt1 == 0 ||
            input.shadow_mask_array == 0 ||
            input.history_current == 0 ||
            input.history_prev == 0 ||
            input.filter_ping[0] == 0 ||
            input.filter_ping[1] == 0)
        {
            return false;
        }

        const RenderSettings &settings = *input.settings;
        const RenderView &view = *input.effective_view;
        const RenderExtent shadow_extent = ShadowMaskExtent(settings.render_extent);
        if (!shadow_extent.IsValid())
        {
            return false;
        }

        const uint32_t layer_count = static_cast<uint32_t>(input.light_store->ShadowCasters().size());
        if (layer_count == 0)
        {
            // No shadow casters — filtered buffer is irrelevant, but leave it
            // in a safe state so a stale dispatch doesn't feed garbage to the
            // deferred shader.
            output.filtered_mask_array = input.shadow_mask_array;
            return true;
        }

        const GLuint groups_x = CeilDiv(shadow_extent.width, kWorkgroupSize);
        const GLuint groups_y = CeilDiv(shadow_extent.height, kWorkgroupSize);

        // ---- Temporal pass ------------------------------------------------
        {
            m_temporal_program->Use();

            glBindImageTexture(kImageBinding,
                               input.history_current,
                               0, GL_TRUE, 0,
                               GL_WRITE_ONLY,
                               GL_R16F);

            BindArrayTextureUnit(kTexUnitMaskIn,  input.shadow_mask_array);
            BindArrayTextureUnit(kTexUnitHistory, input.history_prev);
            BindTexture2DUnit(kTexUnitDepth,      input.gbuffer_depth);

            m_temporal_program->SetUniform1i("u_mask_current",  static_cast<GLint>(kTexUnitMaskIn));
            m_temporal_program->SetUniform1i("u_history_prev",  static_cast<GLint>(kTexUnitHistory));
            m_temporal_program->SetUniform1i("u_gbuffer_depth", static_cast<GLint>(kTexUnitDepth));

            m_temporal_program->SetUniformMat4("u_inv_view",       glm::affineInverse(view.view));
            m_temporal_program->SetUniformMat4("u_inv_projection", glm::inverse(view.projection));
            m_temporal_program->SetUniformMat4("u_prev_view_projection", input.prev_view_projection);

            const GLint output_size_loc = m_temporal_program->GetUniformLocation("u_output_size");
            if (output_size_loc >= 0)
            {
                glUniform2ui(output_size_loc,
                             static_cast<GLuint>(shadow_extent.width),
                             static_cast<GLuint>(shadow_extent.height));
            }
            m_temporal_program->SetUniform1ui("u_layer_count",   layer_count);
            m_temporal_program->SetUniform1f ("u_alpha",         settings.shadow_denoise_temporal_alpha);
            m_temporal_program->SetUniform1ui("u_history_valid", input.history_valid ? 1u : 0u);

            glDispatchCompute(groups_x, groups_y, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        }

        // ---- À-trous iterations -------------------------------------------
        // iter 0 reads history_current and writes filter_ping[0]; subsequent
        // iterations ping-pong between filter_ping[0] and filter_ping[1].
        const int iterations = std::clamp(settings.shadow_denoise_iterations, 0, 5);
        GLuint final_texture = input.history_current;

        if (iterations > 0)
        {
            m_atrous_program->Use();

            BindTexture2DUnit(kTexUnitDepth,  input.gbuffer_depth);
            BindTexture2DUnit(kTexUnitNormal, input.gbuffer_rt1);
            m_atrous_program->SetUniform1i("u_gbuffer_depth", static_cast<GLint>(kTexUnitDepth));
            m_atrous_program->SetUniform1i("u_gbuffer_rt1",   static_cast<GLint>(kTexUnitNormal));

            const GLint output_size_loc = m_atrous_program->GetUniformLocation("u_output_size");
            if (output_size_loc >= 0)
            {
                glUniform2ui(output_size_loc,
                             static_cast<GLuint>(shadow_extent.width),
                             static_cast<GLuint>(shadow_extent.height));
            }
            m_atrous_program->SetUniform1ui("u_layer_count",  layer_count);
            m_atrous_program->SetUniform1f ("u_depth_sigma",  settings.shadow_denoise_depth_sigma);
            m_atrous_program->SetUniform1f ("u_normal_sigma", settings.shadow_denoise_normal_sigma);

            GLuint read_texture  = input.history_current;
            for (int iter = 0; iter < iterations; ++iter)
            {
                const int write_idx = iter % 2;
                GLuint write_texture = input.filter_ping[write_idx];

                BindArrayTextureUnit(kTexUnitMaskIn, read_texture);
                m_atrous_program->SetUniform1i("u_mask_in", static_cast<GLint>(kTexUnitMaskIn));
                m_atrous_program->SetUniform1i("u_stride",  1 << iter);

                glBindImageTexture(kImageBinding,
                                   write_texture,
                                   0, GL_TRUE, 0,
                                   GL_WRITE_ONLY,
                                   GL_R16F);

                glDispatchCompute(groups_x, groups_y, 1);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

                read_texture = write_texture;
            }

            final_texture = read_texture;
        }

        // Leave textures unbound to keep subsequent passes from accidentally
        // inheriting our sampler units.
        BindArrayTextureUnit(kTexUnitHistory, 0);
        BindArrayTextureUnit(kTexUnitMaskIn,  0);
        BindTexture2DUnit(kTexUnitDepth,  0);
        BindTexture2DUnit(kTexUnitNormal, 0);
        glActiveTexture(GL_TEXTURE0);

        GLShaderProgram::Unuse();

        output.filtered_mask_array = final_texture;
        return true;
    }

} // namespace hybrid::renderer
