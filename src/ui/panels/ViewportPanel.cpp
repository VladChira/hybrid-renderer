#include "ViewportPanel.h"

#include "core/Log.h"
#include "ui/UiState.h"

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace hybrid::ui
{
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
        if (context.state != nullptr && context.state->viewport_color_texture != 0)
        {
            const auto &render_extent = context.state->viewport_render_extent;
            const float render_width = static_cast<float>(std::max(render_extent.width, 1u));
            const float render_height = static_cast<float>(std::max(render_extent.height, 1u));
            const float render_aspect = render_width / render_height;
            const float viewport_width = std::max(current_size.x, 1.0f);
            const float viewport_height = std::max(current_size.y, 1.0f);
            const float viewport_aspect = viewport_width / viewport_height;

            ImVec2 image_size = current_size;
            if (viewport_aspect > render_aspect)
            {
                image_size.x = viewport_height * render_aspect;
                image_size.y = viewport_height;
            }
            else
            {
                image_size.x = viewport_width;
                image_size.y = viewport_width / render_aspect;
            }

            const float x_offset = (current_size.x - image_size.x) * 0.5f;
            const float y_offset = (current_size.y - image_size.y) * 0.5f;
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + x_offset,
                                       ImGui::GetCursorPosY() + y_offset));

            ImGui::Image(
                static_cast<ImTextureID>(static_cast<intptr_t>(context.state->viewport_color_texture)),
                image_size,
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f));
        }

        m_last_content_size = current_size;
    }

} // namespace hybrid::ui
