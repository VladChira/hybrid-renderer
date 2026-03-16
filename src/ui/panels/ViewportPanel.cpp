#include "ViewportPanel.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace hybrid::ui
{

    namespace
    {
        bool HasViewportSizeChanged(const ImVec2 &current, const ImVec2 &previous)
        {
            constexpr float epsilon = 0.5f;
            return std::fabs(current.x - previous.x) > epsilon ||
                   std::fabs(current.y - previous.y) > epsilon;
        }
    } // namespace

    ViewportPanel::ViewportPanel()
        : Panel("Viewport")
    {
    }

    void ViewportPanel::DrawContents(PanelContext &context)
    {
        const ImVec2 current_size = ImGui::GetContentRegionAvail();
        if (HasViewportSizeChanged(current_size, m_last_content_size) && context.commands != nullptr)
        {
            UiCommand command{};
            command.type = UiCommand::Type::ViewportResize;
            command.viewport_extent.width = std::max(static_cast<int>(current_size.x), 1);
            command.viewport_extent.height = std::max(static_cast<int>(current_size.y), 1);
            context.commands->push_back(command);
        }
        m_last_content_size = current_size;
    }

} // namespace hybrid::ui
