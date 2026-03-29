#include "SettingsPanel.h"

#include "ui/UiState.h"

#include <imgui.h>

namespace hybrid::ui
{

    namespace
    {
        const char *ToneMapperLabel(renderer::ToneMapper tone_mapper)
        {
            switch (tone_mapper)
            {
            case renderer::ToneMapper::Legacy:
                return "Legacy";
            case renderer::ToneMapper::ACES:
                return "ACES";
            }

            return "Unknown";
        }

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

        renderer::RenderSettings *render_settings = context.state->render_settings;
        if (render_settings == nullptr)
        {
            return;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Tone Mapping");

        int tone_mapper_index = static_cast<int>(render_settings->tone_mapper);
        const char *tone_mapper_options[] = {"Legacy", "ACES"};
        if (ImGui::Combo("Tonemapper", &tone_mapper_index, tone_mapper_options, IM_ARRAYSIZE(tone_mapper_options)))
        {
            render_settings->tone_mapper = static_cast<renderer::ToneMapper>(tone_mapper_index);
        }

        ImGui::SliderFloat("Exposure", &render_settings->exposure, 0.01f, 8.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

        if (render_settings->tone_mapper == renderer::ToneMapper::Legacy)
        {
            ImGui::SliderFloat("Curve Strength", &render_settings->legacy_curve_strength, 0.1f, 4.0f, "%.3f");

            ImGui::SliderFloat("Gamma", &render_settings->legacy_gamma, 1.0f, 3.0f, "%.3f");
        }
        else
        {
            ImGui::SliderFloat("Input Scale", &render_settings->aces_input_scale, 0.1f, 3.0f, "%.3f");

            ImGui::SliderFloat("Saturation", &render_settings->aces_saturation, 0.0f, 2.0f, "%.3f");
        }
    }

} // namespace hybrid::ui
