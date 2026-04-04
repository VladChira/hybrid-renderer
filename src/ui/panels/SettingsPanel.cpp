#include "SettingsPanel.h"

#include "ui/UiState.h"

#include <imgui.h>

namespace hybrid::ui
{

    namespace
    {
        constexpr double kRenderSettingsCommitDebounceSeconds = 0.35;

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

        if (!m_has_pending_render_settings)
        {
            m_pending_render_settings = *render_settings;
            m_has_pending_render_settings = true;
        }

        if (!m_render_settings_dirty)
        {
            // Keep staged settings synced when there is no active local edit.
            m_pending_render_settings = *render_settings;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Tone Mapping");

        auto MarkEdited = [this]()
        {
            m_render_settings_dirty = true;
            m_last_edit_time_seconds = ImGui::GetTime();
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                m_commit_requested = true;
            }
        };

        int tone_mapper_index = static_cast<int>(m_pending_render_settings.tone_mapper);
        const char *tone_mapper_options[] = {"Legacy", "ACES"};
        if (ImGui::Combo("Tonemapper", &tone_mapper_index, tone_mapper_options, IM_ARRAYSIZE(tone_mapper_options)))
        {
            m_pending_render_settings.tone_mapper = static_cast<renderer::ToneMapper>(tone_mapper_index);
            MarkEdited();
        }

        if (ImGui::SliderFloat("Exposure",
                               &m_pending_render_settings.exposure,
                               0.01f,
                               8.0f,
                               "%.3f",
                               ImGuiSliderFlags_Logarithmic))
        {
            MarkEdited();
        }

        if (m_pending_render_settings.tone_mapper == renderer::ToneMapper::Legacy)
        {
            if (ImGui::SliderFloat("Curve Strength", &m_pending_render_settings.legacy_curve_strength, 0.1f, 4.0f, "%.3f"))
            {
                MarkEdited();
            }

            if (ImGui::SliderFloat("Gamma", &m_pending_render_settings.legacy_gamma, 1.0f, 3.0f, "%.3f"))
            {
                MarkEdited();
            }
        }
        else
        {
            if (ImGui::SliderFloat("Input Scale", &m_pending_render_settings.aces_input_scale, 0.1f, 3.0f, "%.3f"))
            {
                MarkEdited();
            }

            if (ImGui::SliderFloat("Saturation", &m_pending_render_settings.aces_saturation, 0.0f, 2.0f, "%.3f"))
            {
                MarkEdited();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("HDRI Precompute");

        int env_cubemap_size = static_cast<int>(m_pending_render_settings.hdri_env_cubemap_size);
        if (ImGui::SliderInt("Env Cubemap Size", &env_cubemap_size, 16, 4096))
        {
            m_pending_render_settings.hdri_env_cubemap_size = static_cast<uint32_t>(env_cubemap_size);
            MarkEdited();
        }

        int irradiance_size = static_cast<int>(m_pending_render_settings.hdri_irradiance_cubemap_size);
        if (ImGui::SliderInt("Irradiance Size", &irradiance_size, 4, 1024))
        {
            m_pending_render_settings.hdri_irradiance_cubemap_size = static_cast<uint32_t>(irradiance_size);
            MarkEdited();
        }

        int prefilter_size = static_cast<int>(m_pending_render_settings.hdri_prefilter_cubemap_size);
        if (ImGui::SliderInt("Prefilter Size", &prefilter_size, 16, 4096))
        {
            m_pending_render_settings.hdri_prefilter_cubemap_size = static_cast<uint32_t>(prefilter_size);
            MarkEdited();
        }

        int prefilter_mips = static_cast<int>(m_pending_render_settings.hdri_prefilter_mip_levels);
        if (ImGui::SliderInt("Prefilter Mips", &prefilter_mips, 1, 12))
        {
            m_pending_render_settings.hdri_prefilter_mip_levels = static_cast<uint32_t>(prefilter_mips);
            MarkEdited();
        }

        int brdf_lut_size = static_cast<int>(m_pending_render_settings.hdri_brdf_lut_size);
        if (ImGui::SliderInt("BRDF LUT Size", &brdf_lut_size, 16, 4096))
        {
            m_pending_render_settings.hdri_brdf_lut_size = static_cast<uint32_t>(brdf_lut_size);
            MarkEdited();
        }

        if (m_render_settings_dirty)
        {
            const bool debounce_elapsed =
                !ImGui::IsAnyItemActive() &&
                (ImGui::GetTime() - m_last_edit_time_seconds) >= kRenderSettingsCommitDebounceSeconds;
            if (m_commit_requested || debounce_elapsed)
            {
                *render_settings = m_pending_render_settings;
                m_render_settings_dirty = false;
                m_commit_requested = false;
            }
        }
    }

} // namespace hybrid::ui
