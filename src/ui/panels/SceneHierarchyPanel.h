#pragma once

#include "Panel.h"

namespace hybrid::ui
{

    class SceneHierarchyPanel final : public Panel
    {
    public:
        SceneHierarchyPanel();

    private:
        void DrawContents(PanelContext &context) override;
    };

} // namespace hybrid::ui
