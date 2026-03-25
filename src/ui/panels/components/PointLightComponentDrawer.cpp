#include "PointLightComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{

    void DrawPointLightComponent(entt::entity entity,
                                 const core::scene::PointLightComponent &component,
                                 CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("Point Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        float range = component.range;
        float attenuation_constant = component.attenuation_constant;
        float attenuation_linear = component.attenuation_linear;
        float attenuation_quadratic = component.attenuation_quadratic;

        bool changed = false;
        changed |= ImGui::DragFloat("Range", &range, 0.05f, 0.0f, 100000.0f, "%.3f");
        changed |= ImGui::DragFloat("Attenuation Constant", &attenuation_constant, 0.01f, 0.0f, 1000.0f, "%.3f");
        changed |= ImGui::DragFloat("Attenuation Linear", &attenuation_linear, 0.01f, 0.0f, 1000.0f, "%.3f");
        changed |= ImGui::DragFloat("Attenuation Quadratic", &attenuation_quadratic, 0.01f, 0.0f, 1000.0f, "%.3f");

        if (!changed || commands == nullptr)
        {
            return;
        }

        EditPointLightCommand command{};
        command.entity = entity;
        command.range = range;
        command.attenuation_constant = attenuation_constant;
        command.attenuation_linear = attenuation_linear;
        command.attenuation_quadratic = attenuation_quadratic;
        EnqueueCommand(*commands, command);
    }

} // namespace hybrid::ui
