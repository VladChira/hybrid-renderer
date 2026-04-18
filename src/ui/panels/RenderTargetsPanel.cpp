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

        constexpr std::array<TargetEntry, 6> kTargets = {{
            {"Final color",            UiViewportVisualization::FinalColor},
            {"G-buffer RT0 (albedo+metallic)", UiViewportVisualization::GBufferRt0},
            {"G-buffer RT1 (normal+roughness)", UiViewportVisualization::GBufferRt1},
            {"G-buffer depth",         UiViewportVisualization::GBufferDepth},
            {"G-buffer entity id",     UiViewportVisualization::GBufferEntityId},
            {"BVH traversal heatmap",  UiViewportVisualization::RaytraceHeatmap},
        }};

        uint64_t ResolveTextureForPreview(const UiState &state, UiViewportVisualization visualization)
        {
            switch (visualization)
            {
            case UiViewportVisualization::FinalColor:      return state.viewport_color_texture;
            case UiViewportVisualization::GBufferRt0:      return state.viewport_gbuffer_rt0_texture;
            case UiViewportVisualization::GBufferRt1:      return state.viewport_gbuffer_rt1_texture;
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

        ImGui::Separator();

        const uint64_t preview_texture = ResolveTextureForPreview(*context.state, *context.viewport_visualization);
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
