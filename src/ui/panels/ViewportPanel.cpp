#include "ViewportPanel.h"

#include "core/Log.h"
#include "ui/UiState.h"

#include <algorithm>
#include <cstdint>
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

    ImGuiWindowFlags ViewportPanel::WindowFlags() const
    {
        return ImGuiWindowFlags_NoBackground;
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

        if (context.state != nullptr && context.state->viewport_color_texture != 0)
        {
            ImGui::Image(
                static_cast<ImTextureID>(static_cast<intptr_t>(context.state->viewport_color_texture)),
                current_size,
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f));
        }

        m_last_content_size = current_size;
    }

} // namespace hybrid::ui
