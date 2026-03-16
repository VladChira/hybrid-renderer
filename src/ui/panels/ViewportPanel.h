#pragma once

#include "Panel.h"

#include <imgui.h>

namespace hybrid::ui
{

    class ViewportPanel final : public Panel
    {
    public:
        ViewportPanel();

    private:
        void DrawContents(PanelContext &context) override;

        ImVec2 m_last_content_size{0.0f, 0.0f};
    };

} // namespace hybrid::ui
