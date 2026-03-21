#include "SettingsPanel.h"

#include "ui/UiState.h"

#include <imgui.h>

namespace hybrid::ui
{

    namespace
    {
        const char *VisualizationLabel(UiViewportVisualization visualization)
        {
            switch (visualization)
            {
            case UiViewportVisualization::FinalColor:
                return "Final Color";
            case UiViewportVisualization::GBufferRt0:
                return "GBuffer RT0 (Albedo/Metallic)";
            case UiViewportVisualization::GBufferRt1:
                return "GBuffer RT1 (Normal/Roughness)";
            }

            return "Unknown";
        }
    } // namespace

    SettingsPanel::SettingsPanel()
        : Panel("Settings")
    {
    }

    void SettingsPanel::DrawContents(PanelContext &context)
    {
        if (context.viewport_visualization == nullptr || context.state == nullptr)
        {
            ImGui::TextUnformatted("Viewport settings unavailable.");
            return;
        }

        UiViewportVisualization visualization = *context.viewport_visualization;
        int selected_index = static_cast<int>(visualization);
        const char *options[] = {
            "Final Color",
            "GBuffer RT0 (Albedo/Metallic)",
            "GBuffer RT1 (Normal/Roughness)"};

        ImGui::TextUnformatted("Viewport Output");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##viewport_output", &selected_index, options, IM_ARRAYSIZE(options)))
        {
            *context.viewport_visualization = static_cast<UiViewportVisualization>(selected_index);
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Current:");
        ImGui::SameLine();
        ImGui::TextUnformatted(VisualizationLabel(*context.viewport_visualization));
    }

} // namespace hybrid::ui
