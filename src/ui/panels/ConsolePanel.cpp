#include "ConsolePanel.h"

#include "core/Log.h"

#include <imgui.h>

namespace hybrid::ui
{
    namespace
    {
        ImVec4 ColorForLine(const std::string &line)
        {
            if (line.find("[critical]") != std::string::npos || line.find("[error]") != std::string::npos)
            {
                return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
            if (line.find("[warn]") != std::string::npos)
            {
                return ImVec4(1.0f, 0.75f, 0.0f, 1.0f);
            }
            if (line.find("[info]") != std::string::npos)
            {
                return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            }
            if (line.find("[debug]") != std::string::npos)
            {
                return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
            }
            if (line.find("[trace]") != std::string::npos)
            {
                return ImVec4(0.6f, 0.8f, 1.0f, 1.0f);
            }

            return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
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
                const ImVec4 color = ColorForLine(line);
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
