#include "CameraComponentDrawer.h"

#include <imgui.h>

#include <glm/glm.hpp>

namespace hybrid::ui
{

    void DrawCameraComponent(const core::scene::CameraComponent &component)
    {
        if (!ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::Text("Horizontal FOV: %.2f deg", glm::degrees(component.horizontal_fov_radians));
        ImGui::Text("Aspect Ratio: %.3f", component.aspect_ratio);
        ImGui::Text("Near Plane: %.4f", component.near_plane);
        ImGui::Text("Far Plane: %.4f", component.far_plane);
    }

} // namespace hybrid::ui
