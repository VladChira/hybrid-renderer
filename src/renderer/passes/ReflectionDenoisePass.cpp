#include "renderer/passes/ReflectionDenoisePass.h"

#include "core/Profiling.h"
#include "renderer/opengl/GLShaderProgram.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kWorkgroupSize = 8;
        constexpr GLuint kImageBinding  = 0;

        // Temporal stage texture units
        constexpr GLuint kTemporalCurrentTexUnit   = 0;
        constexpr GLuint kTemporalHistoryTexUnit   = 1;
        constexpr GLuint kTemporalDepthTexUnit     = 2;
        constexpr GLuint kTemporalNormalTexUnit    = 3;
        constexpr GLuint kTemporalDepthPrevTexUnit = 4;
        constexpr GLuint kTemporalNormalPrevTexUnit = 5;

        // A-Trous stage texture units
        constexpr GLuint kAtrousInputTexUnit  = 0;
        constexpr GLuint kAtrousNormalTexUnit = 1;
        constexpr GLuint kAtrousDepthTexUnit  = 2;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    } // namespace

    ReflectionDenoisePass::ReflectionDenoisePass(GLShaderProgram *temporal_program,
                                                 GLShaderProgram *atrous_program)
        : m_temporal_program(temporal_program),
          m_atrous_program(atrous_program)
    {
    }

    bool ReflectionDenoisePass::Execute(const ReflectionDenoisePassInput &input,
                                        ReflectionDenoisePassOutput       &output)
    {
        HYBRID_PROFILE_ZONE_N("ReflectionDenoisePass::Execute");
        HYBRID_PROFILE_GL_ZONE("ReflectionDenoisePass");

        if (m_temporal_program == nullptr ||
            m_atrous_program   == nullptr ||
            input.effective_view      == nullptr ||
            input.current_radiance    == 0 ||
            input.history_prev        == 0 ||
            input.history_out         == 0 ||
            input.atrous_ping         == 0 ||
            input.atrous_pong         == 0 ||
            input.gbuffer_rt1         == 0 ||
            input.gbuffer_depth       == 0)
        {
            return false;
        }

        const RenderView &view   = *input.effective_view;
        const auto       &extent = input.extent;
        if (!extent.IsValid())
        {
            return false;
        }

        const GLuint groups_x = CeilDiv(extent.width,  kWorkgroupSize);
        const GLuint groups_y = CeilDiv(extent.height, kWorkgroupSize);

        // ------------------------------------------------------------------
        // 1. Temporal accumulation
        // ------------------------------------------------------------------
        {
            HYBRID_PROFILE_ZONE_N("ReflectionDenoisePass::Temporal");
            m_temporal_program->Use();

            const bool prev_gbuffer_available =
                input.gbuffer_depth_prev != 0 && input.gbuffer_rt1_prev != 0;
            const bool history_valid_effective = input.history_valid && prev_gbuffer_available;

            glActiveTexture(GL_TEXTURE0 + kTemporalCurrentTexUnit);
            glBindTexture(GL_TEXTURE_2D, input.current_radiance);
            glActiveTexture(GL_TEXTURE0 + kTemporalHistoryTexUnit);
            glBindTexture(GL_TEXTURE_2D, input.history_prev);
            glActiveTexture(GL_TEXTURE0 + kTemporalDepthTexUnit);
            glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);
            glActiveTexture(GL_TEXTURE0 + kTemporalNormalTexUnit);
            glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt1);
            glActiveTexture(GL_TEXTURE0 + kTemporalDepthPrevTexUnit);
            glBindTexture(GL_TEXTURE_2D,
                          prev_gbuffer_available ? input.gbuffer_depth_prev : input.gbuffer_depth);
            glActiveTexture(GL_TEXTURE0 + kTemporalNormalPrevTexUnit);
            glBindTexture(GL_TEXTURE_2D,
                          prev_gbuffer_available ? input.gbuffer_rt1_prev : input.gbuffer_rt1);

            m_temporal_program->SetUniform1i("u_radiance_current",  static_cast<GLint>(kTemporalCurrentTexUnit));
            m_temporal_program->SetUniform1i("u_history_prev",       static_cast<GLint>(kTemporalHistoryTexUnit));
            m_temporal_program->SetUniform1i("u_gbuffer_depth",      static_cast<GLint>(kTemporalDepthTexUnit));
            m_temporal_program->SetUniform1i("u_gbuffer_rt1",        static_cast<GLint>(kTemporalNormalTexUnit));
            m_temporal_program->SetUniform1i("u_gbuffer_depth_prev", static_cast<GLint>(kTemporalDepthPrevTexUnit));
            m_temporal_program->SetUniform1i("u_gbuffer_rt1_prev",   static_cast<GLint>(kTemporalNormalPrevTexUnit));

            m_temporal_program->SetUniformMat4("u_inv_view",              glm::affineInverse(view.view));
            m_temporal_program->SetUniformMat4("u_inv_projection",        glm::inverse(view.projection));
            m_temporal_program->SetUniformMat4("u_prev_view_projection",  input.prev_view_projection);
            m_temporal_program->SetUniform1f("u_alpha",            input.temporal_alpha);
            m_temporal_program->SetUniform1ui("u_history_valid",   history_valid_effective ? 1u : 0u);
            m_temporal_program->SetUniform1f("u_camera_near",      input.camera_near);
            m_temporal_program->SetUniform1f("u_camera_far",       input.camera_far);
            m_temporal_program->SetUniform1f("u_depth_tolerance",  input.depth_tolerance);
            m_temporal_program->SetUniform1f("u_normal_tolerance", input.normal_tolerance);

            const GLint output_size_loc = m_temporal_program->GetUniformLocation("u_output_size");
            if (output_size_loc >= 0)
            {
                glUniform2ui(output_size_loc,
                             static_cast<GLuint>(extent.width),
                             static_cast<GLuint>(extent.height));
            }

            glBindImageTexture(kImageBinding,
                               input.history_out,
                               0, GL_FALSE, 0,
                               GL_WRITE_ONLY, GL_RGBA16F);

            glDispatchCompute(groups_x, groups_y, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            glActiveTexture(GL_TEXTURE0 + kTemporalNormalPrevTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + kTemporalDepthPrevTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + kTemporalNormalTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + kTemporalDepthTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + kTemporalHistoryTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + kTemporalCurrentTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            GLShaderProgram::Unuse();
        }

        // ------------------------------------------------------------------
        // 2. A-Trous spatial filter (ping-pong)
        // ------------------------------------------------------------------
        GlTextureId atrous_source = input.history_out;
        GlTextureId atrous_target = input.atrous_ping;

        const uint32_t iterations = input.atrous_iterations;
        if (iterations > 0u)
        {
            HYBRID_PROFILE_ZONE_N("ReflectionDenoisePass::Atrous");
            m_atrous_program->Use();

            glActiveTexture(GL_TEXTURE0 + kAtrousNormalTexUnit);
            glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt1);
            glActiveTexture(GL_TEXTURE0 + kAtrousDepthTexUnit);
            glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);
            m_atrous_program->SetUniform1i("u_gbuffer_rt1",   static_cast<GLint>(kAtrousNormalTexUnit));
            m_atrous_program->SetUniform1i("u_gbuffer_depth", static_cast<GLint>(kAtrousDepthTexUnit));
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
                const int stepwidth = 1 << static_cast<int>(iteration);
                m_atrous_program->SetUniform1i("u_stepwidth", stepwidth);

                glActiveTexture(GL_TEXTURE0 + kAtrousInputTexUnit);
                glBindTexture(GL_TEXTURE_2D, atrous_source);
                m_atrous_program->SetUniform1i("u_input_radiance", static_cast<GLint>(kAtrousInputTexUnit));

                glBindImageTexture(kImageBinding,
                                   atrous_target,
                                   0, GL_FALSE, 0,
                                   GL_WRITE_ONLY, GL_RGBA16F);

                glDispatchCompute(groups_x, groups_y, 1);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

                atrous_source = atrous_target;
                atrous_target = (atrous_target == input.atrous_ping)
                                    ? input.atrous_pong
                                    : input.atrous_ping;
            }

            glActiveTexture(GL_TEXTURE0 + kAtrousInputTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + kAtrousDepthTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0 + kAtrousNormalTexUnit);
            glBindTexture(GL_TEXTURE_2D, 0);
            GLShaderProgram::Unuse();
        }

        output.denoised = atrous_source;
        return true;
    }

} // namespace hybrid::renderer
