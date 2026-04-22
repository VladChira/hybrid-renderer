#pragma once

#include "renderer/RendererTypes.h"

#include <memory>

namespace hybrid::renderer
{
    class GLShaderProgram;

    struct AreaLightVisualizationPassInput
    {
        const RenderSettings *settings = nullptr;
        const FrameSceneData *scene_data = nullptr;
        const RenderView *effective_view = nullptr;
        uint32_t scene_framebuffer_id = 0;
        uint32_t gbuffer_framebuffer_id = 0;
    };

    class AreaLightVisualizationPass final
    {
    public:
        explicit AreaLightVisualizationPass(GLShaderProgram *area_light_visualization_shader);
        ~AreaLightVisualizationPass();

        const char *Name() const;
        bool Execute(const AreaLightVisualizationPassInput &input);

    private:
        struct Impl;

        GLShaderProgram *m_area_light_visualization_shader = nullptr;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer
