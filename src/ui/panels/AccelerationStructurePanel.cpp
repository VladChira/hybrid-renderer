#include "AccelerationStructurePanel.h"

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
        (void)context;
        if (m_stats == nullptr)
        {
            ImGui::TextUnformatted("No stats source bound.");
            return;
        }

        const auto &s = *m_stats;

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
