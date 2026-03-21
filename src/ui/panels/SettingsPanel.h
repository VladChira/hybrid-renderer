#pragma once

#include "ui/panels/Panel.h"

namespace hybrid::ui
{

    class SettingsPanel final : public Panel
    {
    public:
        SettingsPanel();

    private:
        void DrawContents(PanelContext &context) override;
    };

} // namespace hybrid::ui
