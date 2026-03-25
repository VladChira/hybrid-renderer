#include "HdriLightComponentDrawer.h"

#include <imgui.h>

#include <glm/glm.hpp>

namespace hybrid::ui
{

    void DrawHdriLightComponent(entt::entity entity,
                                const core::scene::HdriLightComponent &component,
                                CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("HDRI Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        float yaw_radians = component.yaw_radians;
        if (!ImGui::DragFloat("Yaw (rad)", &yaw_radians, 0.01f, -100.0f, 100.0f, "%.3f"))
        {
            ImGui::Text("Yaw (deg): %.3f", glm::degrees(component.yaw_radians));
            return;
        }

        if (commands != nullptr)
        {
            EditHdriLightCommand command{};
            command.entity = entity;
            command.yaw_radians = yaw_radians;
            EnqueueCommand(*commands, command);
        }

        ImGui::Text("Yaw (deg): %.3f", glm::degrees(yaw_radians));
    }

} // namespace hybrid::ui
