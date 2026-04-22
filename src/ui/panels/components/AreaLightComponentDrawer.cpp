#include "AreaLightComponentDrawer.h"

#include <imgui.h>
#include <glm/glm.hpp>

namespace hybrid::ui
{

    void DrawAreaLightComponent(entt::entity entity,
                                const core::scene::AreaLightComponent &component,
                                CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("Area Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        float size[2] = {component.size.x, component.size.y};
        float direction[3] = {component.direction.x, component.direction.y, component.direction.z};
        bool two_sided = component.two_sided;
        bool visible = component.visible;

        bool changed = false;
        changed |= ImGui::DragFloat2("Size", size, 0.01f, 0.0f, 10000.0f, "%.3f");
        changed |= ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f, "%.3f");
        changed |= ImGui::Checkbox("Two-Sided", &two_sided);
        changed |= ImGui::Checkbox("Visible", &visible);

        if (!changed || commands == nullptr)
        {
            return;
        }

        EditAreaLightCommand command{};
        command.entity = entity;
        command.size = glm::vec2(size[0], size[1]);
        command.direction = glm::vec3(direction[0], direction[1], direction[2]);
        command.two_sided = two_sided;
        command.visible = visible;
        EnqueueCommand(*commands, command);
    }

} // namespace hybrid::ui
