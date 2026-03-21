#include "CameraComponentDrawer.h"

#include <imgui.h>

#include <glm/glm.hpp>

namespace hybrid::ui
{

    void DrawCameraComponent(entt::entity entity,
                             const core::scene::CameraComponent &component,
                             CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("Camera Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        float horizontal_fov_degrees = glm::degrees(component.horizontal_fov_radians);
        float aspect_ratio = component.aspect_ratio;
        float near_plane = component.near_plane;
        float far_plane = component.far_plane;

        bool changed = false;
        changed |= ImGui::DragFloat("Horizontal FOV (deg)", &horizontal_fov_degrees, 0.1f, 1.0f, 170.0f, "%.2f");
        changed |= ImGui::DragFloat("Aspect Ratio", &aspect_ratio, 0.01f, 0.01f, 10.0f, "%.3f");
        changed |= ImGui::DragFloat("Near Plane", &near_plane, 0.01f, 0.001f, 100.0f, "%.4f");
        changed |= ImGui::DragFloat("Far Plane", &far_plane, 1.0f, 0.1f, 100000.0f, "%.3f");

        if (!changed || commands == nullptr)
        {
            return;
        }

        CameraSetLensCommand command{};
        command.entity = entity;
        command.horizontal_fov_radians = glm::radians(horizontal_fov_degrees);
        command.near_plane = near_plane;
        command.far_plane = far_plane;
        command.override_aspect_ratio = true;
        command.aspect_ratio = aspect_ratio;
        EnqueueCommand(*commands, command);
    }

} // namespace hybrid::ui
