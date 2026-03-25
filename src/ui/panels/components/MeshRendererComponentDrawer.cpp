#include "MeshRendererComponentDrawer.h"

#include "core/scene/types/SceneAssets.h"

#include <imgui.h>

namespace hybrid::ui
{

    void DrawMeshRendererComponent(const core::scene::MeshRendererComponent &component)
    {
        if (!ImGui::CollapsingHeader("Mesh Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        const auto *mesh = component.mesh.Get();
        if (!mesh)
        {
            ImGui::TextUnformatted("Mesh: <unloaded>");
            return;
        }

        if (mesh->name.empty())
        {
            ImGui::TextUnformatted("Mesh: <unnamed>");
            return;
        }

        ImGui::Text("Mesh: %s", mesh->name.c_str());
        ImGui::Text("Material: %s", component.mesh.Get()->primitives[0].material.Get()->name.c_str());
    }

} // namespace hybrid::ui
