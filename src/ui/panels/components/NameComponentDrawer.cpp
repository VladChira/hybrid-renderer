#include "NameComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{

    void DrawNameComponent(const core::scene::NameComponent &component)
    {
        if (!ImGui::CollapsingHeader("Name", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        const char *label = component.name.empty() ? "<unnamed>" : component.name.c_str();
        ImGui::TextUnformatted(label);
    }

} // namespace hybrid::ui
