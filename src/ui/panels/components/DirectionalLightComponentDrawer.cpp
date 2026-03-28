#include "DirectionalLightComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{

    void DrawDirectionalLightComponent()
    {
        if (!ImGui::CollapsingHeader("Directional Light Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::TextUnformatted("Direction comes from Transform rotation.");
        ImGui::TextUnformatted("Local light axis: -Y.");
    }

} // namespace hybrid::ui
