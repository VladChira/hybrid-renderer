#pragma once

#include "renderer/RendererTypes.h"
#include "renderer/opengl/GLBuffer.h"
#include "renderer/opengl/GLVertexArray.h"

#include <cstdint>

namespace hybrid::renderer
{
    class GLShaderProgram;

    struct AreaLightDebugPassInput
    {
        const RenderSettings *settings        = nullptr;
        const FrameSceneData *scene_data      = nullptr;
        const RenderView     *effective_view  = nullptr;
        uint32_t              scene_framebuffer_id = 0;
        GlTextureId           gbuffer_depth   = 0;
    };

    // Draws a coloured rectangle in world space for each area light flagged
    // with `visible = true`. Purely diagnostic — does not contribute to
    // shading. Depth-tests manually against the G-buffer depth.
    class AreaLightDebugPass final
    {
    public:
        explicit AreaLightDebugPass(GLShaderProgram *program);
        ~AreaLightDebugPass();

        const char *Name() const { return "AreaLightDebug"; }
        bool Execute(const AreaLightDebugPassInput &input);

    private:
        bool EnsureGpuResources();

        GLShaderProgram *m_program = nullptr;
        GLBuffer         m_vertex_buffer{};
        GLVertexArray    m_vao{};
        size_t           m_capacity_bytes = 0;
        bool             m_initialized = false;
    };

} // namespace hybrid::renderer
