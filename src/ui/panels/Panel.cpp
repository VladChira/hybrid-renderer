#include "Panel.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <utility>

namespace hybrid::ui
{

    Panel::Panel(std::string title)
        : m_title(std::move(title))
    {
    }

    const std::string &Panel::Title() const noexcept
    {
        return m_title;
    }

    bool Panel::IsOpen() const noexcept
    {
        return m_is_open;
    }

    void Panel::SetOpen(bool open) noexcept
    {
        m_is_open = open;
    }

    ImGuiWindowFlags Panel::WindowFlags() const
    {
        return 0;
    }

    ImGuiDockNodeFlags Panel::DockNodeFlags() const
    {
        return ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton;
    }

    void Panel::Render(PanelContext &context)
    {
        if (!m_is_open)
        {
            return;
        }

        if (const ImGuiDockNodeFlags dock_flags = DockNodeFlags(); dock_flags != 0)
        {
            ImGuiWindowClass window_class{};
            window_class.DockNodeFlagsOverrideSet = dock_flags;
            ImGui::SetNextWindowClass(&window_class);
        }

        if (ImGui::Begin(m_title.c_str(), &m_is_open, WindowFlags()))
        {
            DrawContents(context);
        }
        ImGui::End();
    }

    void PanelRegistry::Register(std::unique_ptr<Panel> panel)
    {
        if (!panel)
        {
            return;
        }
        m_panels.push_back(std::move(panel));
    }

    void PanelRegistry::Clear()
    {
        m_panels.clear();
    }

    bool PanelRegistry::Empty() const noexcept
    {
        return m_panels.empty();
    }

    void PanelRegistry::DrawAll(PanelContext &context) const
    {
        for (auto &panel : m_panels)
        {
            if (panel)
            {
                panel->Render(context);
            }
        }
    }

} // namespace hybrid::ui
