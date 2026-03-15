#pragma once

#include "Panel.h"

namespace hybrid::ui
{

    class PropertiesPanel final : public Panel
    {
    public:
        PropertiesPanel();

    private:
        void DrawContents(PanelContext &context) override;
    };

} // namespace hybrid::ui
