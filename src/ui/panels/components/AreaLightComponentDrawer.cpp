#include "AreaLightComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{

    void DrawAreaLightComponent(const core::scene::AreaLightComponent &component)
    {
        if (!ImGui::CollapsingHeader("Area Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::Text("Size: %.3f, %.3f", component.size.x, component.size.y);
        ImGui::Text("Direction: %.3f, %.3f, %.3f",
                    component.direction.x,
                    component.direction.y,
                    component.direction.z);
        ImGui::Text("Two-Sided: %s", component.two_sided ? "Yes" : "No");
    }

} // namespace hybrid::ui
