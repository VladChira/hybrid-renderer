#include "DirectionalLightComponentDrawer.h"

#include <imgui.h>
#include <glm/glm.hpp>

namespace hybrid::ui
{

    void DrawDirectionalLightComponent(entt::entity entity,
                                       const core::scene::DirectionalLightComponent &component,
                                       CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("Directional Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        float direction[3] = {component.direction.x, component.direction.y, component.direction.z};
        if (!ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f, "%.3f") || commands == nullptr)
        {
            return;
        }

        EditDirectionalLightCommand command{};
        command.entity = entity;
        command.direction = glm::vec3(direction[0], direction[1], direction[2]);
        EnqueueCommand(*commands, command);
    }

} // namespace hybrid::ui
