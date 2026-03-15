#include "TransformComponentDrawer.h"

#include <imgui.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace hybrid::ui
{
    namespace
    {
        void DrawVec3Row(const char *label,
                         const char *id_prefix,
                         float values[3],
                         ImU32 red_bg,
                         ImU32 green_bg,
                         ImU32 blue_bg)
        {
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, 14288647);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);

            ImGui::PushID(id_prefix);

            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, red_bg);
            ImGui::DragFloat("##X", &values[0], 0.01f, 0.0f, 0.0f, "X: %.2f");

            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, green_bg);
            ImGui::DragFloat("##Y", &values[1], 0.01f, 0.0f, 0.0f, "Y: %.2f");

            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, blue_bg);
            ImGui::DragFloat("##Z", &values[2], 0.01f, 0.0f, 0.0f, "Z: %.2f");

            ImGui::PopID();
        }
    } // namespace

    void DrawTransformComponent(const core::scene::TransformComponent &component)
    {
        if (!ImGui::CollapsingHeader("Entity Transform Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::BeginDisabled();
        ImGui::Button("Reset Transform");

        const auto &transform = component.local;

        float position[3] = {transform.translation.x, transform.translation.y, transform.translation.z};
        const glm::vec3 euler = glm::degrees(glm::eulerAngles(transform.rotation));
        float rotation[3] = {euler.x, euler.y, euler.z};
        float scale[3] = {transform.scale.x, transform.scale.y, transform.scale.z};

        if (ImGui::BeginTable("TransformTable", 4, ImGuiTableFlags_Borders))
        {
            const ImU32 red_bg = ImGui::GetColorU32(ImVec4(0.8f, 0.02f, 0.01f, 1.0f));
            const ImU32 green_bg = ImGui::GetColorU32(ImVec4(0.16f, 0.83f, 0.02f, 1.0f));
            const ImU32 blue_bg = ImGui::GetColorU32(ImVec4(0.01f, 0.29f, 0.878f, 1.0f));

            DrawVec3Row("Position", "pos", position, red_bg, green_bg, blue_bg);
            DrawVec3Row("Rotation", "rot", rotation, red_bg, green_bg, blue_bg);
            DrawVec3Row("Scale", "scale", scale, red_bg, green_bg, blue_bg);

            ImGui::EndTable();
        }
        ImGui::EndDisabled();
    }

} // namespace hybrid::ui
