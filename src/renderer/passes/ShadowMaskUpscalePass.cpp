#include "renderer/passes/ShadowMaskUpscalePass.h"

#include "core/Profiling.h"
#include "renderer/opengl/GLShaderProgram.h"

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kWorkgroupSize = 8;
        constexpr GLuint kOutputImageBinding = 0;
        constexpr GLuint kInputMaskTexUnit = 0;
        constexpr GLuint kNormalTexUnit    = 1;
        constexpr GLuint kDepthTexUnit     = 2;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    } // namespace

    ShadowMaskUpscalePass::ShadowMaskUpscalePass(GLShaderProgram *program)
        : m_program(program)
    {
    }

    bool ShadowMaskUpscalePass::Execute(const ShadowMaskUpscalePassInput &input)
    {
        HYBRID_PROFILE_ZONE_N("ShadowMaskUpscalePass::Execute");
        HYBRID_PROFILE_GL_ZONE("ShadowMaskUpscalePass");

        if (m_program == nullptr ||
            input.input_mask_array == 0 ||
            input.output_mask_array == 0 ||
            input.gbuffer_rt1 == 0 ||
            input.gbuffer_depth == 0 ||
            input.layer_count == 0 ||
            !input.input_extent.IsValid() ||
            !input.output_extent.IsValid())
        {
            return false;
        }

        m_program->Use();

        glActiveTexture(GL_TEXTURE0 + kInputMaskTexUnit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, input.input_mask_array);
        glActiveTexture(GL_TEXTURE0 + kNormalTexUnit);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt1);
        glActiveTexture(GL_TEXTURE0 + kDepthTexUnit);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);

        m_program->SetUniform1i("u_input_mask",    static_cast<GLint>(kInputMaskTexUnit));
        m_program->SetUniform1i("u_gbuffer_rt1",   static_cast<GLint>(kNormalTexUnit));
        m_program->SetUniform1i("u_gbuffer_depth", static_cast<GLint>(kDepthTexUnit));
        m_program->SetUniform1ui("u_layer_count", input.layer_count);
        m_program->SetUniform1f("u_depth_sigma", input.depth_sigma);
        m_program->SetUniform1f("u_normal_exponent", input.normal_exponent);

        const GLint output_size_loc = m_program->GetUniformLocation("u_output_size");
        if (output_size_loc >= 0)
        {
            glUniform2ui(output_size_loc,
                         static_cast<GLuint>(input.output_extent.width),
                         static_cast<GLuint>(input.output_extent.height));
        }
        const GLint input_size_loc = m_program->GetUniformLocation("u_input_size");
        if (input_size_loc >= 0)
        {
            glUniform2ui(input_size_loc,
                         static_cast<GLuint>(input.input_extent.width),
                         static_cast<GLuint>(input.input_extent.height));
        }

        glBindImageTexture(kOutputImageBinding,
                           input.output_mask_array,
                           0,
                           GL_TRUE,
                           0,
                           GL_WRITE_ONLY,
                           GL_R8);

        const GLuint groups_x = CeilDiv(input.output_extent.width, kWorkgroupSize);
        const GLuint groups_y = CeilDiv(input.output_extent.height, kWorkgroupSize);
        glDispatchCompute(groups_x, groups_y, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glActiveTexture(GL_TEXTURE0 + kDepthTexUnit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + kNormalTexUnit);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0 + kInputMaskTexUnit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        GLShaderProgram::Unuse();
        return true;
    }

} // namespace hybrid::renderer
