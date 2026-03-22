#include "HdriLightComponentDrawer.h"

#include <imgui.h>

#include <glm/glm.hpp>

namespace hybrid::ui
{

    void DrawHdriLightComponent(const core::scene::HdriLightComponent &component)
    {
        if (!ImGui::CollapsingHeader("HDRI Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::Text("Yaw (rad): %.3f", component.yaw_radians);
        ImGui::Text("Yaw (deg): %.3f", glm::degrees(component.yaw_radians));
    }

} // namespace hybrid::ui
