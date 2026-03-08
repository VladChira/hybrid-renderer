#pragma once

#include "Panel.h"

namespace hybrid::ui
{

    class PerformancePanel final : public Panel
    {
    public:
        PerformancePanel();

    private:
        void DrawContents(PanelContext &context) override;
    };

} // namespace hybrid::ui
