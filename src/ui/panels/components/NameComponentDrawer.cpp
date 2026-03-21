#include "NameComponentDrawer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace hybrid::ui
{

    void DrawNameComponent(entt::entity entity,
                           const core::scene::NameComponent &component,
                           CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("Name##name_component_header", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        std::array<char, 256> buffer{};
        if (!component.name.empty())
        {
            const size_t copy_count = std::min(component.name.size(), buffer.size() - 1);
            std::memcpy(buffer.data(), component.name.data(), copy_count);
        }

        if (ImGui::InputText("Name##name_component_input", buffer.data(), buffer.size()) && commands != nullptr)
        {
            EnqueueCommand(*commands, EntityRenameCommand{entity, std::string(buffer.data())});
        }
    }

} // namespace hybrid::ui
