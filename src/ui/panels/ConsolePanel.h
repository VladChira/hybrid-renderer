#pragma once

#include "Panel.h"

namespace hybrid::ui
{

    class ConsolePanel final : public Panel
    {
    public:
        ConsolePanel();

    private:
        void DrawContents(PanelContext &context) override;
        ImGuiDockNodeFlags DockNodeFlags() const override;
    };

} // namespace hybrid::ui
