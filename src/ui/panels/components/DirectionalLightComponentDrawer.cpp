#include "DirectionalLightComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{

    void DrawDirectionalLightComponent(const core::scene::DirectionalLightComponent &component)
    {
        if (!ImGui::CollapsingHeader("Directional Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::Text("Direction: %.3f, %.3f, %.3f",
                    component.direction.x,
                    component.direction.y,
                    component.direction.z);
    }

} // namespace hybrid::ui
