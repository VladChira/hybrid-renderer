#pragma once

#include "Panel.h"

#include "renderer/raytracing/AccelerationStructureCache.h"

namespace hybrid::ui
{

    // Displays BLAS/TLAS telemetry fed by the Renderer each frame. The
    // Renderer owns the stats; the panel is a read-only view.
    class AccelerationStructurePanel final : public Panel
    {
    public:
        explicit AccelerationStructurePanel(const renderer::raytracing::AccelerationStructureStats *stats);

    private:
        void DrawContents(PanelContext &context) override;

        const renderer::raytracing::AccelerationStructureStats *m_stats = nullptr;
    };

} // namespace hybrid::ui
