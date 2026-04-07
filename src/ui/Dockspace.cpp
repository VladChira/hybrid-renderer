#include "ui/Dockspace.h"

#include "core/Profiling.h"

#include <algorithm>

#include <imgui.h>
#include <imgui_internal.h>

namespace hybrid::ui
{

    DockspaceLayout DockspaceLayout::Default()
    {
        DockspaceLayout layout;
        return layout;
    }

    void Dockspace::BeginFrame() const
    {
        HYBRID_PROFILE_ZONE_N("Dockspace::BeginFrame");
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::DockSpaceOverViewport(viewport->ID,
                                     viewport,
                                     ImGuiDockNodeFlags_PassthruCentralNode);
    }

    void Dockspace::BuildLayout(const DockspaceLayout &layout)
    {
        HYBRID_PROFILE_ZONE_N("Dockspace::BuildLayout");
        if (m_layout_built)
        {
            return;
        }

        ImGuiID dockspace_id = ImGui::GetMainViewport()->ID;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id,
                                  ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_top_id = 0;
        const bool has_top_assignment = std::any_of(layout.assignments.begin(),
                                                    layout.assignments.end(),
                                                    [](const DockAssignment &assignment)
                                                    { return assignment.target == DockTarget::Top; });
        if (has_top_assignment)
        {
            dock_top_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Up, layout.top_ratio, nullptr, &dock_main_id);
        }

        ImGuiID dock_left_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, layout.left_ratio, nullptr, &dock_main_id);

        ImGuiID dock_right_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, layout.right_ratio, nullptr, &dock_main_id);

        ImGuiID dock_bottom_id = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, layout.bottom_ratio, nullptr, &dock_main_id);
        ImGuiID dock_bottom_right_id = ImGui::DockBuilderSplitNode(dock_bottom_id, ImGuiDir_Right, layout.bottom_split_ratio, nullptr, &dock_bottom_id);
        ImGuiID dock_bottom_left_id = dock_bottom_id;

        ImGuiID dock_right_top_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Up, layout.right_split_ratio, nullptr, &dock_right_id);

        ImGuiID dock_left_top_id = ImGui::DockBuilderSplitNode(dock_left_id, ImGuiDir_Up, layout.left_split_ratio, nullptr, &dock_left_id);

        ImGuiID dock_right_bottom_id = dock_right_id;

        ImGuiID dock_left_bottom_id = dock_left_id;

        for (const auto &assignment : layout.assignments)
        {
            ImGuiID target = 0;
            switch (assignment.target)
            {
            case DockTarget::Top:
                target = dock_top_id;
                break;
            case DockTarget::Main:
                target = dock_main_id;
                break;
            case DockTarget::LeftTop:
                target = dock_left_top_id;
                break;
            case DockTarget::LeftBottom:
                target = dock_left_bottom_id;
                break;
            case DockTarget::RightTop:
                target = dock_right_top_id;
                break;
            case DockTarget::RightBottom:
                target = dock_right_bottom_id;
                break;
            case DockTarget::BottomLeft:
                target = dock_bottom_left_id;
                break;
            case DockTarget::BottomRight:
                target = dock_bottom_right_id;
                break;
            }

            ImGui::DockBuilderDockWindow(assignment.panel_title.c_str(), target);
        }

        ImGui::DockBuilderFinish(dockspace_id);
        m_layout_built = true;
    }

    void Dockspace::ResetLayout() noexcept
    {
        m_layout_built = false;
    }

} // namespace hybrid::ui
