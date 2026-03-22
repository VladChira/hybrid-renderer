#include "LightCommonComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{

    void DrawLightCommonComponent(const core::scene::LightCommonComponent &component)
    {
        if (!ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::Text("Color: %.3f, %.3f, %.3f", component.color.x, component.color.y, component.color.z);
        ImGui::Text("Intensity: %.3f", component.intensity);
        ImGui::Text("Cast Shadows: %s", component.cast_shadows ? "Yes" : "No");
    }

} // namespace hybrid::ui
