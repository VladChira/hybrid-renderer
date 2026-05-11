#include "renderer/passes/ShadowMaskReducePass.h"

#include "core/Profiling.h"
#include "renderer/opengl/GLShaderProgram.h"

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kWorkgroupSize    = 8;
        constexpr GLuint kOcclusionBinding = 0;
        constexpr GLuint kShadowMaskUnit   = 0;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    } // namespace

    ShadowMaskReducePass::ShadowMaskReducePass(GLShaderProgram *program)
        : m_program(program)
    {
    }

    bool ShadowMaskReducePass::Execute(const ShadowMaskReducePassInput &input)
    {
        HYBRID_PROFILE_ZONE_N("ShadowMaskReducePass::Execute");
        HYBRID_PROFILE_GL_ZONE("ShadowMaskReducePass");

        if (m_program == nullptr ||
            input.shadow_mask_array == 0 ||
            input.occlusion_out == 0 ||
            !input.extent.IsValid())
        {
            return false;
        }

        m_program->Use();

        glBindImageTexture(kOcclusionBinding,
                           input.occlusion_out,
                           0,
                           GL_FALSE,
                           0,
                           GL_WRITE_ONLY,
                           GL_R8);

        glActiveTexture(GL_TEXTURE0 + kShadowMaskUnit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, input.shadow_mask_array);
        m_program->SetUniform1i("u_shadow_masks", static_cast<int>(kShadowMaskUnit));

        const GLuint groups_x = CeilDiv(input.extent.width,  kWorkgroupSize);
        const GLuint groups_y = CeilDiv(input.extent.height, kWorkgroupSize);
        glDispatchCompute(groups_x, groups_y, 1);

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glActiveTexture(GL_TEXTURE0 + kShadowMaskUnit);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        GLShaderProgram::Unuse();
        return true;
    }

} // namespace hybrid::renderer
