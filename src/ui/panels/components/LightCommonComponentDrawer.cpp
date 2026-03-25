#include "LightCommonComponentDrawer.h"

#include <imgui.h>
#include <glm/glm.hpp>

namespace hybrid::ui
{

    void DrawLightCommonComponent(entt::entity entity,
                                  const core::scene::LightCommonComponent &component,
                                  CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        float color[3] = {component.color.x, component.color.y, component.color.z};
        float intensity = component.intensity;
        bool cast_shadows = component.cast_shadows;

        bool changed = false;
        changed |= ImGui::ColorEdit3("Color", color);
        changed |= ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 100000.0f, "%.3f");
        changed |= ImGui::Checkbox("Cast Shadows", &cast_shadows);

        if (!changed || commands == nullptr)
        {
            return;
        }

        EditLightCommonCommand command{};
        command.entity = entity;
        command.color = glm::vec3(color[0], color[1], color[2]);
        command.intensity = intensity;
        command.cast_shadows = cast_shadows;
        EnqueueCommand(*commands, command);
    }

} // namespace hybrid::ui
