#include "CameraTargetComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{

    void DrawCameraTargetComponent(const core::scene::CameraTargetComponent &component)
    {
        if (!ImGui::CollapsingHeader("Camera Target Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::Text("Enabled: %s", component.enabled ? "true" : "false");
        if (component.target == entt::null)
        {
            ImGui::TextUnformatted("Target: <null>");
        }
        else
        {
            ImGui::Text("Target: %u", entt::to_integral(component.target));
        }
    }

} // namespace hybrid::ui
