#include "renderer/passes/SpatioTemporalDenoisePass.h"

#include "core/Profiling.h"
#include "renderer/opengl/GLShaderProgram.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kWorkgroupSize = 8;
        constexpr GLuint kTemporalHistoryImageBinding = 0;
        constexpr GLuint kAtrousOutputImageBinding = 0;

        constexpr GLuint kTemporalCurrentTexUnit = 0;
        constexpr GLuint kTemporalHistoryTexUnit = 1;
        constexpr GLuint kTemporalDepthTexUnit = 2;

        constexpr GLuint kAtrousInputTexUnit = 0;
        constexpr GLuint kAtrousNormalTexUnit = 1;
        constexpr GLuint kAtrousDepthTexUnit = 2;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    } // namespace

    SpatioTemporalDenoisePass::SpatioTemporalDenoisePass(GLShaderProgram *temporal_program,
                                                         GLShaderProgram *atrous_program)
        : m_temporal_program(temporal_program),
          m_atrous_program(atrous_program)
    {
    }

    bool SpatioTemporalDenoisePass::Execute(const SpatioTemporalDenoisePassInput &input,
                                            SpatioTemporalDenoisePassOutput &output)
    {
        HYBRID_PROFILE_ZONE_N("SpatioTemporalDenoisePass::Execute");
        HYBRID_PROFILE_GL_ZONE("SpatioTemporalDenoisePass");

        if (m_temporal_program == nullptr ||
            m_atrous_program == nullptr ||
            input.effective_view == nullptr ||
            input.current_signal_array == 0 ||
            input.history_prev_array == 0 ||
            input.history_out_array == 0 ||
            input.atrous_ping_array == 0 ||
            input.atrous_pong_array == 0 ||
            input.gbuffer_rt1 == 0 ||
            input.gbuffer_depth == 0 ||
            input.layer_count == 0)
        {
            return false;
        }

        const RenderView &view = *input.effective_view;
        const auto &extent = input.extent;
        if (!extent.IsValid())
        {
            return false;
        }

        const GLuint groups_x = CeilDiv(extent.width, kWorkgroupSize);
        const GLuint groups_y = CeilDiv(extent.height, kWorkgroupSize);

        // ----------------------------
        // 1) Temporal accumulation
        // ----------------------------
        {
            HYBRID_PROFILE_ZONE_N("SpatioTemporalDenoisePass::Temporal");
            m_temporal_program->Use();

            glActiveTexture(GL_TEXTURE0 + kTemporalCurrentTexUnit);
            glBindTexture(GL_TEXTURE_2D_ARRAY, input.current_signal_array);
            glActiveTexture(GL_TEXTURE0 + kTemporalHistoryTexUnit);
            glBindTexture(GL_TEXTURE_2D_ARRAY, input.history_prev_array);
            glActiveTexture(GL_TEXTURE0 + kTemporalDepthTexUnit);
            glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);

            m_temporal_program->SetUniform1i("u_mask_current", static_cast<GLint>(kTemporalCurrentTexUnit));
            m_temporal_program->SetUniform1i("u_history_prev", static_cast<GLint>(kTemporalHistoryTexUnit));
            m_temporal_program->SetUniform1i("u_gbuffer_depth", static_cast<GLint>(kTemporalDepthTexUnit));
            m_temporal_program->SetUniformMat4("u_inv_view", glm::affineInverse(view.view));
            m_temporal_program->SetUniformMat4("u_inv_projection", glm::inverse(view.projection));
            m_temporal_program->SetUniformMat4("u_prev_view_projection", input.prev_view_projection);
            m_temporal_program->SetUniform1ui("u_layer_count", input.layer_count);
            m_temporal_program->SetUniform1ui("u_denoise_layer_mask", input.denoise_layer_mask);
            m_temporal_program->SetUniform1f("u_alpha", input.temporal_alpha);
            m_temporal_program->SetUniform1ui("u_history_valid", input.history_valid ? 1u : 0u);

            const GLint output_size_loc = m_temporal_program->GetUniformLocation("u_output_size");
            if (output_size_loc >= 0)
            {
                glUniform2ui(output_size_loc,
                             static_cast<GLuint>(extent.width),
                             static_cast<GLuint>(extent.height));
            }

            glBindImageTexture(kTemporalHistoryImageBinding,
                               input.history_out_array,
                               0,
                               GL_TRUE,
                               0,
                               GL_WRITE_ONLY,
                               GL_R16F);

            glDispatchCompute(groups_x, groups_y, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            glActiveTexture(GL_TEXTURE0 + kTemporalDepthTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + kTemporalHistoryTexUnit);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            glActiveTexture(GL_TEXTURE0 + kTemporalCurrentTexUnit);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            GLShaderProgram::Unuse();
        }

        GlTextureId atrous_source = input.history_out_array;
        GlTextureId atrous_target = input.atrous_ping_array;

        const uint32_t iterations = input.atrous_iterations;
        if (iterations > 0u)
        {
            HYBRID_PROFILE_ZONE_N("SpatioTemporalDenoisePass::Atrous");
            m_atrous_program->Use();

            glActiveTexture(GL_TEXTURE0 + kAtrousNormalTexUnit);
            glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt1);
            glActiveTexture(GL_TEXTURE0 + kAtrousDepthTexUnit);
            glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);
            m_atrous_program->SetUniform1i("u_gbuffer_rt1", static_cast<GLint>(kAtrousNormalTexUnit));
            m_atrous_program->SetUniform1i("u_gbuffer_depth", static_cast<GLint>(kAtrousDepthTexUnit));

            m_atrous_program->SetUniform1ui("u_layer_count", input.layer_count);
            m_atrous_program->SetUniform1ui("u_denoise_layer_mask", input.denoise_layer_mask);
            m_atrous_program->SetUniform1f("u_c_phi", input.c_phi);
            m_atrous_program->SetUniform1f("u_n_phi", input.n_phi);
            m_atrous_program->SetUniform1f("u_p_phi", input.p_phi);

            const GLint output_size_loc = m_atrous_program->GetUniformLocation("u_output_size");
            if (output_size_loc >= 0)
            {
                glUniform2ui(output_size_loc,
                             static_cast<GLuint>(extent.width),
                             static_cast<GLuint>(extent.height));
            }

            for (uint32_t iteration = 0; iteration < iterations; ++iteration)
            {
                const int stepwidth = 1 << iteration;
                m_atrous_program->SetUniform1i("u_stepwidth", stepwidth);

                glActiveTexture(GL_TEXTURE0 + kAtrousInputTexUnit);
                glBindTexture(GL_TEXTURE_2D_ARRAY, atrous_source);
                m_atrous_program->SetUniform1i("u_input_signal", static_cast<GLint>(kAtrousInputTexUnit));

                glBindImageTexture(kAtrousOutputImageBinding,
                                   atrous_target,
                                   0,
                                   GL_TRUE,
                                   0,
                                   GL_WRITE_ONLY,
                                   GL_R16F);

                glDispatchCompute(groups_x, groups_y, 1);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

                atrous_source = atrous_target;
                atrous_target = (atrous_target == input.atrous_ping_array)
                                    ? input.atrous_pong_array
                                    : input.atrous_ping_array;
            }

            glActiveTexture(GL_TEXTURE0 + kAtrousInputTexUnit);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            glActiveTexture(GL_TEXTURE0 + kAtrousDepthTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + kAtrousNormalTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            GLShaderProgram::Unuse();
        }

        output.history = input.history_out_array;
        output.denoised = atrous_source;
        return true;
    }

} // namespace hybrid::renderer
