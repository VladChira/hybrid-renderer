#include "renderer/passes/DeferredLightingPass.h"

#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLVertexArray.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer
{

    struct DeferredLightingPass::Impl
    {
        GLVertexArray fullscreen_vao{};
    };

    DeferredLightingPass::DeferredLightingPass(GLShaderProgram *deferred_shader)
        : m_deferred_shader(deferred_shader),
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
        if (m_deferred_shader == nullptr ||
            m_impl == nullptr ||
            input.settings == nullptr ||
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
        m_deferred_shader->SetUniformMat4("u_inv_view", glm::affineInverse(effective_view.view));
        m_deferred_shader->SetUniformMat4("u_inv_projection", glm::inverse(effective_view.projection));
        m_deferred_shader->SetUniformVec3("u_camera_position", effective_view.position);
        m_deferred_shader->SetUniform1f("u_exposure", settings.exposure);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);

        m_impl->fullscreen_vao.Bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
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
