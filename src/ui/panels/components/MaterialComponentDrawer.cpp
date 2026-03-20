#include "MaterialComponentDrawer.h"

#include "core/scene/types/SceneAssets.h"

#include <imgui.h>

namespace hybrid::ui
{
    namespace
    {
        const char *ToString(core::scene::AlphaMode mode)
        {
            switch (mode)
            {
            case core::scene::AlphaMode::Opaque:
                return "Opaque";
            case core::scene::AlphaMode::Mask:
                return "Mask";
            case core::scene::AlphaMode::Blend:
                return "Blend";
            }

            return "Unknown";
        }
    } // namespace

    void DrawMaterialComponent(const core::scene::MaterialAsset &material)
    {
        if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        const char *name = material.name.empty() ? "<unnamed>" : material.name.c_str();
        ImGui::Text("Name: %s", name);
        ImGui::Separator();

        ImGui::Text("Base Color: %.3f %.3f %.3f %.3f",
                    material.base_color_factor.r,
                    material.base_color_factor.g,
                    material.base_color_factor.b,
                    material.base_color_factor.a);
        ImGui::Text("Metallic: %.3f", material.metallic_factor);
        ImGui::Text("Roughness: %.3f", material.roughness_factor);
        ImGui::Text("Emissive: %.3f %.3f %.3f",
                    material.emissive_factor.r,
                    material.emissive_factor.g,
                    material.emissive_factor.b);
        ImGui::Text("Alpha Mode: %s", ToString(material.alpha_mode));
        ImGui::Text("Alpha Cutoff: %.3f", material.alpha_cutoff);
        ImGui::Text("Double Sided: %s", material.double_sided ? "Yes" : "No");
    }

} // namespace hybrid::ui
