#pragma once

#include "RendererTypes.h"

namespace hybrid::renderer::utils
{
    inline RenderExtent ToRenderExtent(int width, int height)
    {
        renderer::RenderExtent extent{};
        extent.width = static_cast<uint32_t>(std::max(width, 1));
        extent.height = static_cast<uint32_t>(std::max(height, 1));
        return extent;
    }

} // namespace hybrid::renderer::utils