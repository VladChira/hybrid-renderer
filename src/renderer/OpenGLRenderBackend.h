#pragma once

#include "renderer/RendererTypes.h"

namespace hybrid::renderer
{

    class OpenGLRenderBackend
    {
    public:
        bool BeginFrame(uint32_t scene_framebuffer_id, const RenderExtent &extent) const;
        void EndFrame() const;
    };

} // namespace hybrid::renderer
