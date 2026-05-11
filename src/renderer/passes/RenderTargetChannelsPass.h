#pragma once

#include "renderer/RendererTypes.h"

#include <memory>

namespace hybrid::renderer
{
    class GLShaderProgram;

    struct RenderTargetChannelsPassInput
    {
        RenderExtent extent{};
        uint32_t debug_framebuffer_id = 0;
        const RenderView *effective_view = nullptr;
        GlTextureId source_color = 0;
        GlTextureId source_gbuffer_rt0 = 0;
        GlTextureId source_gbuffer_rt1 = 0;
        GlTextureId source_gbuffer_depth = 0;
        RenderChannelOutputs out_color_channels{};
        RenderChannelOutputs out_gbuffer_rt0_channels{};
        RenderChannelOutputs out_gbuffer_rt1_channels{};
        GlTextureId out_gbuffer_depth_linear = 0;
    };

    class RenderTargetChannelsPass final
    {
    public:
        explicit RenderTargetChannelsPass(GLShaderProgram *extract_shader);
        ~RenderTargetChannelsPass();

        const char *Name() const;
        bool Execute(const RenderTargetChannelsPassInput &input);

    private:
        struct Impl;
        GLShaderProgram *m_extract_shader = nullptr;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer
