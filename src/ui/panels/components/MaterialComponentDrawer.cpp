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

    void DrawMaterialComponent(const core::scene::MaterialAsset &material,
                               uint64_t material_id,
                               CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        const char *name = material.name.empty() ? "<unnamed>" : material.name.c_str();
        ImGui::Text("Name: %s", name);
        ImGui::Separator();

        float base_color[4] = {material.base_color_factor.r,
                               material.base_color_factor.g,
                               material.base_color_factor.b,
                               material.base_color_factor.a};
        float metallic_factor = material.metallic_factor;
        float roughness_factor = material.roughness_factor;

        if (ImGui::ColorPicker4("Base Color", base_color))
        {
            EnqueueCommand(*commands, MaterialSetVec4Command{material_id, MaterialVec4Property::BaseColorFactor,
                                                             glm::vec4(base_color[0], base_color[1],
                                                                       base_color[2], base_color[3])});
        }
        ImGui::DragFloat("Metallic", &metallic_factor, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Roughness", &roughness_factor, 0.01f, 0.0f, 1.0f);

        ImGui::Text("Emissive: %.3f %.3f %.3f",
                    material.emissive_factor.r,
                    material.emissive_factor.g,
                    material.emissive_factor.b);
        ImGui::Text("Alpha Mode: %s", ToString(material.alpha_mode));
        ImGui::Text("Alpha Cutoff: %.3f", material.alpha_cutoff);
        ImGui::Text("Double Sided: %s", material.double_sided ? "Yes" : "No");
    }

} // namespace hybrid::ui
