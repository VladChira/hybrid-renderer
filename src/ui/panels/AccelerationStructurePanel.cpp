#include "AccelerationStructurePanel.h"

#include "renderer/RendererTypes.h"
#include "ui/UiState.h"

#include <imgui.h>

#include <cstddef>

namespace hybrid::ui
{

    namespace
    {
        void DrawBytes(const char *label, size_t bytes)
        {
            const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
            ImGui::Text("%-24s %.2f MB (%zu B)", label, mb, bytes);
        }
    } // namespace

    AccelerationStructurePanel::AccelerationStructurePanel(
        const renderer::raytracing::AccelerationStructureStats *stats)
        : Panel("Acceleration Structures"),
          m_stats(stats)
    {
    }

    void AccelerationStructurePanel::DrawContents(PanelContext &context)
    {
        if (m_stats == nullptr)
        {
            ImGui::TextUnformatted("No stats source bound.");
            return;
        }

        const auto &s = *m_stats;

        renderer::RenderSettings *settings = (context.state != nullptr) ? context.state->render_settings : nullptr;

        if (ImGui::CollapsingHeader("Ray-traced shadows", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (settings != nullptr)
            {
                ImGui::Checkbox("Enabled", &settings->enable_raytrace_shadows);
                ImGui::SliderFloat("Normal bias",
                                   &settings->raytrace_shadow_normal_bias,
                                   0.001f,
                                   0.25f,
                                   "%.4f",
                                   ImGuiSliderFlags_Logarithmic);

                ImGui::Separator();
                ImGui::Checkbox("Denoise", &settings->enable_shadow_denoise);
                if (settings->enable_shadow_denoise)
                {
                    ImGui::SliderFloat("Temporal alpha",
                                       &settings->shadow_denoise_temporal_alpha,
                                       0.0f, 0.99f, "%.3f");
                    ImGui::SliderInt("A-trous iterations",
                                     &settings->shadow_denoise_iterations,
                                     0, 5);
                    ImGui::SliderFloat("Depth sigma",
                                       &settings->shadow_denoise_depth_sigma,
                                       0.0001f, 1.0f, "%.4f",
                                       ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat("Normal sigma",
                                       &settings->shadow_denoise_normal_sigma,
                                       1.0f, 256.0f, "%.1f",
                                       ImGuiSliderFlags_Logarithmic);
                }
                else
                {
                    ImGui::TextDisabled("1-spp raw masks; expect per-frame noise on area lights.");
                }
            }
            else
            {
                ImGui::TextUnformatted("Render settings unavailable.");
            }
        }

        if (ImGui::CollapsingHeader("Screen-space GI (BVH-hybrid)", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (settings != nullptr)
            {
                ImGui::Checkbox("Enabled##ssgi", &settings->enable_ssgi);
                ImGui::SliderFloat("Intensity##ssgi",
                                   &settings->ssgi_intensity,
                                   0.0f, 4.0f, "%.2f");
                ImGui::SliderFloat("Max ray distance##ssgi",
                                   &settings->ssgi_max_ray_distance,
                                   0.5f, 200.0f, "%.1f",
                                   ImGuiSliderFlags_Logarithmic);
                ImGui::SliderFloat("Screen thickness##ssgi",
                                   &settings->ssgi_screen_thickness,
                                   0.0005f, 0.1f, "%.4f",
                                   ImGuiSliderFlags_Logarithmic);

                ImGui::Separator();
                ImGui::Checkbox("Denoise##ssgi", &settings->enable_ssgi_denoise);
                if (settings->enable_ssgi_denoise)
                {
                    ImGui::SliderFloat("Temporal alpha##ssgi",
                                       &settings->ssgi_denoise_temporal_alpha,
                                       0.0f, 0.99f, "%.3f");
                    ImGui::SliderInt("A-trous iterations##ssgi",
                                     &settings->ssgi_denoise_iterations,
                                     0, 5);
                    ImGui::SliderFloat("Depth sigma##ssgi",
                                       &settings->ssgi_denoise_depth_sigma,
                                       0.0001f, 1.0f, "%.4f",
                                       ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat("Normal sigma##ssgi",
                                       &settings->ssgi_denoise_normal_sigma,
                                       1.0f, 256.0f, "%.1f",
                                       ImGuiSliderFlags_Logarithmic);
                }
                else
                {
                    ImGui::TextDisabled("1-spp raw GI; expect per-frame noise.");
                }
            }
            else
            {
                ImGui::TextUnformatted("Render settings unavailable.");
            }
        }

        if (ImGui::CollapsingHeader("Traversal heatmap", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (settings != nullptr)
            {
                ImGui::TextUnformatted(settings->enable_raytrace_heatmap
                                           ? "Active — pick another target to stop dispatch."
                                           : "Inactive — select the heatmap target to enable.");
                ImGui::SliderFloat("Scale",
                                   &settings->raytrace_heatmap_scale,
                                   1.0f,
                                   4096.0f,
                                   "%.0f",
                                   ImGuiSliderFlags_Logarithmic);
                ImGui::TextDisabled("Visit count divided by this before the colour LUT.");
            }
            else
            {
                ImGui::TextUnformatted("Render settings unavailable.");
            }
        }

        if (ImGui::CollapsingHeader("BLAS", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Count              %u", s.blas_count);
            ImGui::Text("Total nodes        %u", s.blas_total_nodes);
            ImGui::Text("Total leaves       %u", s.blas_total_leaves);
            ImGui::Text("Total triangles    %u", s.blas_total_triangles);
            ImGui::Text("Max depth          %u", s.blas_max_depth);
            ImGui::Text("Cumulative build   %.2f ms", s.blas_build_ms_total);
        }

        if (ImGui::CollapsingHeader("TLAS", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Nodes              %u", s.tlas_nodes);
            ImGui::Text("Leaves             %u", s.tlas_leaves);
            ImGui::Text("Max depth          %u", s.tlas_max_depth);
            ImGui::Text("Instances          %u", s.tlas_instances);
            ImGui::Text("Last build         %.3f ms", s.tlas_build_ms);
        }

        if (ImGui::CollapsingHeader("GPU memory", ImGuiTreeNodeFlags_DefaultOpen))
        {
            DrawBytes("BLAS nodes",     s.gpu_blas_nodes_bytes);
            DrawBytes("BLAS triangles", s.gpu_blas_triangles_bytes);
            DrawBytes("TLAS nodes",     s.gpu_tlas_nodes_bytes);
            DrawBytes("TLAS instances", s.gpu_tlas_instances_bytes);
        }
    }

} // namespace hybrid::ui
