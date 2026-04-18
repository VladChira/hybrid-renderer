#include "renderer/passes/SsgiDenoisePass.h"

#include "core/Profiling.h"
#include "renderer/FrameResources.h"
#include "renderer/opengl/GLShaderProgram.h"

#include <algorithm>

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kWorkgroupSize       = 8;
        constexpr GLuint kImageBinding        = 0;
        constexpr GLuint kTexUnitMain         = 0;
        constexpr GLuint kTexUnitHistoryPrev  = 1;
        constexpr GLuint kTexUnitDepth        = 2;
        constexpr GLuint kTexUnitNormal       = 3;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }

        void BindTex(GLuint unit, GLuint id)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, id);
        }
    } // namespace

    SsgiDenoisePass::SsgiDenoisePass(GLShaderProgram *temporal_program,
                                     GLShaderProgram *atrous_program)
        : m_temporal_program(temporal_program),
          m_atrous_program(atrous_program)
    {
    }

    bool SsgiDenoisePass::Execute(const SsgiDenoisePassInput &input, SsgiDenoisePassOutput &output)
    {
        HYBRID_PROFILE_ZONE_N("SsgiDenoisePass::Execute");
        HYBRID_PROFILE_GL_ZONE("SsgiDenoisePass");

        output.filtered = 0;

        if (m_temporal_program == nullptr ||
            m_atrous_program == nullptr ||
            input.settings == nullptr ||
            input.effective_view == nullptr ||
            input.gbuffer_depth == 0 ||
            input.gbuffer_rt1 == 0 ||
            input.ssgi_raw == 0 ||
            input.history_current == 0 ||
            input.history_prev == 0 ||
            input.filter_ping[0] == 0 ||
            input.filter_ping[1] == 0)
        {
            return false;
        }

        const RenderSettings &settings = *input.settings;
        const RenderView &view = *input.effective_view;
        const RenderExtent ssgi_extent = SsgiExtent(settings.render_extent);
        if (!ssgi_extent.IsValid())
        {
            return false;
        }

        const GLuint groups_x = CeilDiv(ssgi_extent.width,  kWorkgroupSize);
        const GLuint groups_y = CeilDiv(ssgi_extent.height, kWorkgroupSize);

        // ---------------- Temporal ------------------------------------------
        {
            m_temporal_program->Use();
            glBindImageTexture(kImageBinding, input.history_current, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            BindTex(kTexUnitMain,        input.ssgi_raw);
            BindTex(kTexUnitHistoryPrev, input.history_prev);
            BindTex(kTexUnitDepth,       input.gbuffer_depth);

            m_temporal_program->SetUniform1i("u_current",        static_cast<GLint>(kTexUnitMain));
            m_temporal_program->SetUniform1i("u_history_prev",   static_cast<GLint>(kTexUnitHistoryPrev));
            m_temporal_program->SetUniform1i("u_gbuffer_depth",  static_cast<GLint>(kTexUnitDepth));

            m_temporal_program->SetUniformMat4("u_inv_view",             glm::affineInverse(view.view));
            m_temporal_program->SetUniformMat4("u_inv_projection",       glm::inverse(view.projection));
            m_temporal_program->SetUniformMat4("u_prev_view_projection", input.prev_view_projection);

            const GLint output_size_loc = m_temporal_program->GetUniformLocation("u_output_size");
            if (output_size_loc >= 0)
            {
                glUniform2ui(output_size_loc,
                             static_cast<GLuint>(ssgi_extent.width),
                             static_cast<GLuint>(ssgi_extent.height));
            }
            m_temporal_program->SetUniform1f ("u_alpha",         settings.ssgi_denoise_temporal_alpha);
            m_temporal_program->SetUniform1ui("u_history_valid", input.history_valid ? 1u : 0u);

            glDispatchCompute(groups_x, groups_y, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        }

        // ---------------- À-trous iterations --------------------------------
        const int iterations = std::clamp(settings.ssgi_denoise_iterations, 0, 5);
        GLuint final_texture = input.history_current;

        if (iterations > 0)
        {
            m_atrous_program->Use();

            BindTex(kTexUnitDepth,  input.gbuffer_depth);
            BindTex(kTexUnitNormal, input.gbuffer_rt1);
            m_atrous_program->SetUniform1i("u_gbuffer_depth", static_cast<GLint>(kTexUnitDepth));
            m_atrous_program->SetUniform1i("u_gbuffer_rt1",   static_cast<GLint>(kTexUnitNormal));

            const GLint output_size_loc = m_atrous_program->GetUniformLocation("u_output_size");
            if (output_size_loc >= 0)
            {
                glUniform2ui(output_size_loc,
                             static_cast<GLuint>(ssgi_extent.width),
                             static_cast<GLuint>(ssgi_extent.height));
            }
            m_atrous_program->SetUniform1f("u_depth_sigma",  settings.ssgi_denoise_depth_sigma);
            m_atrous_program->SetUniform1f("u_normal_sigma", settings.ssgi_denoise_normal_sigma);

            GLuint read_texture = input.history_current;
            for (int iter = 0; iter < iterations; ++iter)
            {
                const int write_idx = iter % 2;
                GLuint write_texture = input.filter_ping[write_idx];

                BindTex(kTexUnitMain, read_texture);
                m_atrous_program->SetUniform1i("u_ssgi_in", static_cast<GLint>(kTexUnitMain));
                m_atrous_program->SetUniform1i("u_stride",  1 << iter);

                glBindImageTexture(kImageBinding,
                                   write_texture,
                                   0, GL_FALSE, 0,
                                   GL_WRITE_ONLY,
                                   GL_RGBA16F);

                glDispatchCompute(groups_x, groups_y, 1);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

                read_texture = write_texture;
            }
            final_texture = read_texture;
        }

        BindTex(kTexUnitNormal,      0);
        BindTex(kTexUnitDepth,       0);
        BindTex(kTexUnitHistoryPrev, 0);
        BindTex(kTexUnitMain,        0);
        glActiveTexture(GL_TEXTURE0);
        GLShaderProgram::Unuse();

        output.filtered = final_texture;
        return true;
    }

} // namespace hybrid::renderer
