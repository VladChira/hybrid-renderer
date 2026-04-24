#include "RenderTargetsPanel.h"

#include "ui/UiState.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace hybrid::ui
{

    namespace
    {
        struct TargetEntry
        {
            const char *label;
            UiViewportVisualization visualization;
        };

        struct ChannelEntry
        {
            const char *label;
        };

        constexpr std::array<TargetEntry, 6> kTargets = {{
            {"Final color",            UiViewportVisualization::FinalColor},
            {"G-buffer RT0 (albedo+metallic)", UiViewportVisualization::GBufferRt0},
            {"G-buffer RT1 (normal+roughness)", UiViewportVisualization::GBufferRt1},
            {"G-buffer depth",         UiViewportVisualization::GBufferDepth},
            {"G-buffer entity id",     UiViewportVisualization::GBufferEntityId},
            {"BVH traversal heatmap",  UiViewportVisualization::RaytraceHeatmap},
        }};

        constexpr std::array<ChannelEntry, 6> kChannels = {{
            {"RGBA"},
            {"RGB"},
            {"R"},
            {"G"},
            {"B"},
            {"A"},
        }};

        bool SupportsChannelSelection(UiViewportVisualization visualization)
        {
            return visualization == UiViewportVisualization::FinalColor ||
                   visualization == UiViewportVisualization::GBufferRt0 ||
                   visualization == UiViewportVisualization::GBufferRt1;
        }

        uint64_t ResolveChannelTexture(const UiChannelTextures &channels, int channel_index)
        {
            switch (channel_index)
            {
            case 1: return channels.rgb;
            case 2: return channels.r;
            case 3: return channels.g;
            case 4: return channels.b;
            case 5: return channels.a;
            default: return 0;
            }
        }

        uint64_t ResolveTextureForPreview(const UiState &state,
                                          UiViewportVisualization visualization,
                                          int channel_index)
        {
            switch (visualization)
            {
            case UiViewportVisualization::FinalColor:
                return channel_index == 0 ? state.viewport_color_texture
                                          : ResolveChannelTexture(state.viewport_color_channels, channel_index);
            case UiViewportVisualization::GBufferRt0:
                return channel_index == 0 ? state.viewport_gbuffer_rt0_texture
                                          : ResolveChannelTexture(state.viewport_gbuffer_rt0_channels, channel_index);
            case UiViewportVisualization::GBufferRt1:
                return channel_index == 0 ? state.viewport_gbuffer_rt1_texture
                                          : ResolveChannelTexture(state.viewport_gbuffer_rt1_channels, channel_index);
            case UiViewportVisualization::GBufferDepth:    return state.viewport_gbuffer_depth_texture;
            case UiViewportVisualization::GBufferEntityId: return state.viewport_entity_id_texture;
            case UiViewportVisualization::RaytraceHeatmap: return state.viewport_raytrace_heatmap_texture;
            }
            return 0;
        }
    } // namespace

    RenderTargetsPanel::RenderTargetsPanel()
        : Panel("Render Targets")
    {
    }

    void RenderTargetsPanel::DrawContents(PanelContext &context)
    {
        if (context.viewport_visualization == nullptr || context.state == nullptr)
        {
            ImGui::TextUnformatted("No renderer state available.");
            return;
        }

        UiViewportVisualization current = *context.viewport_visualization;
        int current_index = 0;
        for (size_t i = 0; i < kTargets.size(); ++i)
        {
            if (kTargets[i].visualization == current)
            {
                current_index = static_cast<int>(i);
                break;
            }
        }

        if (ImGui::BeginCombo("Viewport output", kTargets[current_index].label))
        {
            for (size_t i = 0; i < kTargets.size(); ++i)
            {
                const bool selected = static_cast<size_t>(current_index) == i;
                if (ImGui::Selectable(kTargets[i].label, selected))
                {
                    *context.viewport_visualization = kTargets[i].visualization;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        const bool supports_channel_selection = SupportsChannelSelection(*context.viewport_visualization);
        if (supports_channel_selection)
        {
            m_preview_channel_index = std::clamp(m_preview_channel_index, 0, static_cast<int>(kChannels.size()) - 1);
            if (ImGui::BeginCombo("Channel", kChannels[static_cast<size_t>(m_preview_channel_index)].label))
            {
                for (size_t i = 0; i < kChannels.size(); ++i)
                {
                    const bool selected = static_cast<int>(i) == m_preview_channel_index;
                    if (ImGui::Selectable(kChannels[i].label, selected))
                    {
                        m_preview_channel_index = static_cast<int>(i);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            m_preview_channel_index = 0;
            ImGui::TextUnformatted("Channel: n/a (single-channel target)");
        }

        ImGui::Separator();

        const uint64_t preview_texture =
            ResolveTextureForPreview(*context.state, *context.viewport_visualization, m_preview_channel_index);
        if (preview_texture == 0)
        {
            ImGui::TextUnformatted("Target is not available this frame.");
            return;
        }

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float preview_width = std::max(avail.x, 1.0f);
        const auto &extent = context.state->viewport_render_extent;
        const float render_width  = static_cast<float>(extent.width  > 0 ? extent.width  : 1);
        const float render_height = static_cast<float>(extent.height > 0 ? extent.height : 1);
        const float aspect = render_width / render_height;
        const float preview_height = preview_width / aspect;

        ImGui::Image(
            static_cast<ImTextureID>(static_cast<intptr_t>(preview_texture)),
            ImVec2(preview_width, preview_height),
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
    }

} // namespace hybrid::ui
