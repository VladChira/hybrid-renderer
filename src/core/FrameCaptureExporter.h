#pragma once

#include "renderer/RendererTypes.h"

#include <cstdint>
#include <string>

namespace hybrid::core
{

    struct FrameCaptureExportResult
    {
        bool success = false;
        std::string directory;
        uint32_t files_written = 0;
    };

    FrameCaptureExportResult ExportFrameCapture(const renderer::RendererOutputs &outputs,
                                                const renderer::RenderExtent &extent,
                                                const renderer::RenderView &view);

} // namespace hybrid::core
