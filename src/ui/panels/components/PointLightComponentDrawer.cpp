#include "PointLightComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{

    void DrawPointLightComponent(const core::scene::PointLightComponent &component)
    {
        if (!ImGui::CollapsingHeader("Point Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        if (component.range > 0.0f)
        {
            ImGui::Text("Range: %.3f", component.range);
        }
        else
        {
            ImGui::TextUnformatted("Range: <unspecified>");
        }
        ImGui::Text("Attenuation Constant: %.3f", component.attenuation_constant);
        ImGui::Text("Attenuation Linear: %.3f", component.attenuation_linear);
        ImGui::Text("Attenuation Quadratic: %.3f", component.attenuation_quadratic);
    }

} // namespace hybrid::ui
