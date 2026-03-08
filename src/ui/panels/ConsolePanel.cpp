#include "ConsolePanel.h"

#include "core/Log.h"
#include "ui/themes/Themes.h"

#include <imgui.h>

namespace hybrid::ui
{
    namespace
    {
        ImVec4 ColorForLine(const std::string &line, const ThemePalette *theme)
        {
            const ImVec4 fallback = theme ? theme->log.fallback : ImGui::GetStyle().Colors[ImGuiCol_Text];
            if (line.find("[critical]") != std::string::npos)
            {
                return theme ? theme->log.critical : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            if (line.find("[error]") != std::string::npos)
            {
                return theme ? theme->log.error : ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            if (line.find("[warn]") != std::string::npos)
            {
                return theme ? theme->log.warn : ImVec4(1.0f, 0.75f, 0.0f, 1.0f);
            }
            if (line.find("[info]") != std::string::npos)
            {
                return theme ? theme->log.info : fallback;
            }
            if (line.find("[debug]") != std::string::npos)
            {
                return theme ? theme->log.debug : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            }
            if (line.find("[trace]") != std::string::npos)
            {
                return theme ? theme->log.trace : ImVec4(0.6f, 0.8f, 1.0f, 1.0f);
            }

            return fallback;
        }
    } // namespace

    ConsolePanel::ConsolePanel()
        : Panel("Console")
    {
    }

    void ConsolePanel::DrawContents(PanelContext &context)
    {
        (void)context;

        const float bottom_space = ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginChild("ScrollRegion##Console", ImVec2(0, -bottom_space), false, 0))
        {
            ImGui::PushTextWrapPos();

            const auto lines = core::Log::GetInMemoryLog();
            for (const auto &line : lines)
            {
                const ImVec4 color = ColorForLine(line, context.theme);
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(line.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::PopTextWrapPos();

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }

} // namespace hybrid::ui
