#include "MaterialsPanel.h"

#include "ui/UiState.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace hybrid::ui
{
    namespace
    {
        std::string BuildMaterialLabel(const UiMaterialEntry &material, size_t index)
        {
            if (!material.name.empty())
            {
                return material.name;
            }

            return "Material " + std::to_string(index + 1);
        }
    } // namespace

    MaterialsPanel::MaterialsPanel()
        : Panel("Materials")
    {
    }

    void MaterialsPanel::DrawContents(PanelContext &context)
    {
        if (!context.state)
        {
            ImGui::TextUnformatted("UI state unavailable.");
            return;
        }

        const auto &materials = context.state->materials;
        if (materials.empty())
        {
            if (!context.state->scene_world)
            {
                ImGui::TextUnformatted("No scene loaded.");
            }
            else
            {
                ImGui::TextUnformatted("No materials found in scene.");
            }
            return;
        }

        const float cell_width = 78.0f;
        const float preview_height = 78.0f;
        const float item_spacing = ImGui::GetStyle().ItemSpacing.x;
        const float available_width = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, static_cast<int>((available_width + item_spacing) / (cell_width + item_spacing)));

        for (size_t i = 0; i < materials.size(); ++i)
        {
            const UiMaterialEntry &material = materials[i];
            const bool same_line = (i % static_cast<size_t>(columns)) != 0;
            if (same_line)
            {
                ImGui::SameLine();
            }

            ImGui::PushID(static_cast<int>(material.asset_id != 0 ? material.asset_id : i + 1));
            ImGui::BeginGroup();

            ImGui::InvisibleButton("##material_preview", ImVec2(cell_width, preview_height));
            if (ImGui::IsItemClicked() && context.selection)
            {
                context.selection->type = UiSelection::Type::Material;
                context.selection->entity = entt::null;
                context.selection->material_asset_id = material.asset_id;
            }

            const ImVec2 preview_min = ImGui::GetItemRectMin();
            const ImVec2 preview_max = ImGui::GetItemRectMax();
            ImDrawList *draw_list = ImGui::GetWindowDrawList();

            const bool is_selected =
                context.selection &&
                context.selection->type == UiSelection::Type::Material &&
                context.selection->material_asset_id == material.asset_id;

            const ImU32 fill_color = IM_COL32(28, 28, 28, 255);
            const ImU32 border_color = is_selected ? IM_COL32(230, 180, 70, 255) : IM_COL32(90, 90, 90, 255);
            const ImU32 cross_color = IM_COL32(120, 120, 120, 140);
            draw_list->AddRectFilled(preview_min, preview_max, fill_color, 4.0f);
            draw_list->AddRect(preview_min, preview_max, border_color, 4.0f, 0, is_selected ? 2.0f : 1.0f);
            draw_list->AddLine(preview_min, preview_max, cross_color, 1.0f);
            draw_list->AddLine(ImVec2(preview_min.x, preview_max.y), ImVec2(preview_max.x, preview_min.y), cross_color, 1.0f);

            const std::string label = BuildMaterialLabel(material, i);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cell_width);
            ImGui::TextUnformatted(label.c_str());
            ImGui::PopTextWrapPos();

            ImGui::EndGroup();
            ImGui::PopID();
        }
    }

} // namespace hybrid::ui
