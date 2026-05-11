#include "renderer/passes/RenderTargetChannelsPass.h"

#include "core/Profiling.h"
#include "renderer/opengl/GLFramebuffer.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLVertexArray.h"

#include <array>
#include <cstddef>

namespace hybrid::renderer
{

    struct RenderTargetChannelsPass::Impl
    {
        GLVertexArray fullscreen_vao{};
    };

    namespace
    {
        bool HasAllChannels(const RenderChannelOutputs &channels)
        {
            return channels.rgb != 0 &&
                   channels.r != 0 &&
                   channels.g != 0 &&
                   channels.b != 0 &&
                   channels.a != 0;
        }
    } // namespace

    RenderTargetChannelsPass::RenderTargetChannelsPass(GLShaderProgram *extract_shader)
        : m_extract_shader(extract_shader),
          m_impl(std::make_unique<Impl>())
    {
    }

    RenderTargetChannelsPass::~RenderTargetChannelsPass() = default;

    const char *RenderTargetChannelsPass::Name() const
    {
        return "RenderTargetChannels";
    }

    bool RenderTargetChannelsPass::Execute(const RenderTargetChannelsPassInput &input)
    {
        HYBRID_PROFILE_ZONE_N("RenderTargetChannelsPass::Execute");
        HYBRID_PROFILE_GL_ZONE("RenderTargetChannelsPass");

        if (m_extract_shader == nullptr ||
            m_impl == nullptr ||
            !input.extent.IsValid() ||
            input.debug_framebuffer_id == 0 ||
            input.effective_view == nullptr ||
            input.source_color == 0 ||
            input.source_gbuffer_rt0 == 0 ||
            input.source_gbuffer_rt1 == 0 ||
            input.source_gbuffer_depth == 0 ||
            input.out_gbuffer_depth_linear == 0 ||
            !HasAllChannels(input.out_color_channels) ||
            !HasAllChannels(input.out_gbuffer_rt0_channels) ||
            !HasAllChannels(input.out_gbuffer_rt1_channels))
        {
            return false;
        }

        if (!m_impl->fullscreen_vao.IsValid() && !m_impl->fullscreen_vao.Create())
        {
            return false;
        }

        struct ExtractionBatch
        {
            GlTextureId source = 0;
            RenderChannelOutputs outputs{};
        };

        const std::array<ExtractionBatch, 3> batches{{
            {input.source_color, input.out_color_channels},
            {input.source_gbuffer_rt0, input.out_gbuffer_rt0_channels},
            {input.source_gbuffer_rt1, input.out_gbuffer_rt1_channels},
        }};

        glBindFramebuffer(GL_FRAMEBUFFER, input.debug_framebuffer_id);
        glViewport(0, 0, static_cast<GLsizei>(input.extent.width), static_cast<GLsizei>(input.extent.height));
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        m_extract_shader->Use();
        m_extract_shader->SetUniform1i("u_source_texture", 0);
        m_extract_shader->SetUniform1i("u_output_mode", 0);
        m_extract_shader->SetUniform1f("u_depth_near_plane", input.effective_view->near_plane);
        m_extract_shader->SetUniform1f("u_depth_far_plane", input.effective_view->far_plane);

        for (const ExtractionBatch &batch : batches)
        {
            const std::array<GlTextureId, 5> destination_channels{{
                batch.outputs.rgb,
                batch.outputs.r,
                batch.outputs.g,
                batch.outputs.b,
                batch.outputs.a,
            }};
            const std::array<int, 5> channel_modes{{-1, 0, 1, 2, 3}};

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, batch.source);

            for (std::size_t channel_index = 0; channel_index < destination_channels.size(); ++channel_index)
            {
                const GlTextureId destination = destination_channels[channel_index];
                glFramebufferTexture2D(GL_FRAMEBUFFER,
                                       GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D,
                                       destination,
                                       0);
                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                {
                    GLShaderProgram::Unuse();
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glDepthMask(GL_TRUE);
                    GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
                    return false;
                }

                m_extract_shader->SetUniform1i("u_channel_index", channel_modes[channel_index]);
                m_impl->fullscreen_vao.Bind();
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, input.source_gbuffer_depth);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,
                               input.out_gbuffer_depth_linear,
                               0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            GLShaderProgram::Unuse();
            glBindTexture(GL_TEXTURE_2D, 0);
            glDepthMask(GL_TRUE);
            GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
            return false;
        }

        m_extract_shader->SetUniform1i("u_output_mode", 1);
        m_extract_shader->SetUniform1i("u_channel_index", 0);
        m_impl->fullscreen_vao.Bind();
        glDrawArrays(GL_TRIANGLES, 0, 3);

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDepthMask(GL_TRUE);
        GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
        return true;
    }

} // namespace hybrid::renderer
