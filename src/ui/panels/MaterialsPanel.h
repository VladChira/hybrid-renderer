#pragma once

#include "ui/panels/Panel.h"

namespace hybrid::ui
{

    class MaterialsPanel final : public Panel
    {
    public:
        MaterialsPanel();

    private:
        void DrawContents(PanelContext &context) override;
    };

} // namespace hybrid::ui
