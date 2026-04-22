#pragma once

#include "Panel.h"

namespace hybrid::ui
{

    // Small control panel that drives which render target the Viewport panel
    // displays. Extended in later phases as ray-tracing outputs (shadow masks,
    // reflection buffers, BVH heatmaps, etc.) become available.
    class RenderTargetsPanel final : public Panel
    {
    public:
        RenderTargetsPanel();

    private:
        void DrawContents(PanelContext &context) override;

        int m_preview_channel_index = 0;
    };

} // namespace hybrid::ui
