#include "CameraTargetComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{

    void DrawCameraTargetComponent(entt::entity entity,
                                   const core::scene::CameraTargetComponent &component,
                                   CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("Camera Target Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        bool enabled = component.enabled;
        int target_id = component.target == entt::null ? -1 : static_cast<int>(entt::to_integral(component.target));
        bool changed = false;

        changed |= ImGui::Checkbox("Enabled", &enabled);
        changed |= ImGui::InputInt("Target Entity Id", &target_id);
        ImGui::TextUnformatted("Use -1 for no target.");

        if (!changed || commands == nullptr)
        {
            return;
        }

        CameraSetTargetCommand command{};
        command.entity = entity;
        command.enabled = enabled;
        command.target = target_id < 0 ? entt::null : static_cast<entt::entity>(target_id);
        EnqueueCommand(*commands, command);
    }

} // namespace hybrid::ui
