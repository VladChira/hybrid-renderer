#include "TransformComponentDrawer.h"

#include <imgui.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace hybrid::ui
{
    namespace
    {
        struct RotationUiState
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            bool initialized = false;
        };

        RotationUiState LoadRotationUiState(entt::entity entity)
        {
            ImGuiStorage const *storage = ImGui::GetStateStorage();
            ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
            const ImGuiID key_x = ImGui::GetID("TransformRotX");
            const ImGuiID key_y = ImGui::GetID("TransformRotY");
            const ImGuiID key_z = ImGui::GetID("TransformRotZ");
            const ImGuiID key_initialized = ImGui::GetID("TransformRotInitialized");
            ImGui::PopID();

            RotationUiState state;
            state.x = storage->GetFloat(key_x, 0.0f);
            state.y = storage->GetFloat(key_y, 0.0f);
            state.z = storage->GetFloat(key_z, 0.0f);
            state.initialized = storage->GetBool(key_initialized, false);
            return state;
        }

        void SaveRotationUiState(entt::entity entity, const RotationUiState &state)
        {
            ImGuiStorage *storage = ImGui::GetStateStorage();
            ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
            const ImGuiID key_x = ImGui::GetID("TransformRotX");
            const ImGuiID key_y = ImGui::GetID("TransformRotY");
            const ImGuiID key_z = ImGui::GetID("TransformRotZ");
            const ImGuiID key_initialized = ImGui::GetID("TransformRotInitialized");
            ImGui::PopID();

            storage->SetFloat(key_x, state.x);
            storage->SetFloat(key_y, state.y);
            storage->SetFloat(key_z, state.z);
            storage->SetBool(key_initialized, state.initialized);
        }

        glm::quat ApplyEulerDeltaDegrees(const glm::quat &source, const glm::vec3 &delta_degrees)
        {
            glm::quat result = source;
            if (delta_degrees.x != 0.0f)
            {
                result = glm::normalize(result * glm::angleAxis(glm::radians(delta_degrees.x), glm::vec3(1.0f, 0.0f, 0.0f)));
            }
            if (delta_degrees.y != 0.0f)
            {
                result = glm::normalize(result * glm::angleAxis(glm::radians(delta_degrees.y), glm::vec3(0.0f, 1.0f, 0.0f)));
            }
            if (delta_degrees.z != 0.0f)
            {
                result = glm::normalize(result * glm::angleAxis(glm::radians(delta_degrees.z), glm::vec3(0.0f, 0.0f, 1.0f)));
            }
            return result;
        }

        bool DrawVec3Row(const char *label,
                         const char *id_prefix,
                         float values[3],
                         float speed,
                         ImU32 red_bg,
                         ImU32 green_bg,
                         ImU32 blue_bg)
        {
            bool changed = false;
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, 14288647);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);

            ImGui::PushID(id_prefix);

            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, red_bg);
            changed |= ImGui::DragFloat("##X", &values[0], speed, 0.0f, 0.0f, "X: %.2f");

            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, green_bg);
            changed |= ImGui::DragFloat("##Y", &values[1], speed, 0.0f, 0.0f, "Y: %.2f");

            ImGui::TableNextColumn();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, blue_bg);
            changed |= ImGui::DragFloat("##Z", &values[2], speed, 0.0f, 0.0f, "Z: %.2f");

            ImGui::PopID();
            return changed;
        }
    } // namespace

    void DrawTransformComponent(entt::entity entity,
                                const core::scene::TransformComponent &component,
                                CommandBuffer *commands)
    {
        if (!ImGui::CollapsingHeader("Entity Transform Component", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        const auto &transform = component.local;

        float position[3] = {transform.translation.x, transform.translation.y, transform.translation.z};
        RotationUiState rotation_state = LoadRotationUiState(entity);
        if (!rotation_state.initialized)
        {
            const glm::vec3 euler = glm::degrees(glm::eulerAngles(transform.rotation));
            rotation_state.x = euler.x;
            rotation_state.y = euler.y;
            rotation_state.z = euler.z;
            rotation_state.initialized = true;
        }
        float rotation[3] = {rotation_state.x, rotation_state.y, rotation_state.z};
        const float previous_rotation[3] = {rotation[0], rotation[1], rotation[2]};
        float scale[3] = {transform.scale.x, transform.scale.y, transform.scale.z};
        bool changed = false;
        bool rotation_changed = false;
        bool reset_clicked = false;

        if (ImGui::Button("Reset Transform"))
        {
            reset_clicked = true;
            position[0] = 0.0f;
            position[1] = 0.0f;
            position[2] = 0.0f;
            rotation[0] = 0.0f;
            rotation[1] = 0.0f;
            rotation[2] = 0.0f;
            scale[0] = 1.0f;
            scale[1] = 1.0f;
            scale[2] = 1.0f;
            changed = true;
        }

        if (ImGui::BeginTable("TransformTable", 4, ImGuiTableFlags_Borders))
        {
            const ImU32 red_bg = ImGui::GetColorU32(ImVec4(0.8f, 0.02f, 0.01f, 1.0f));
            const ImU32 green_bg = ImGui::GetColorU32(ImVec4(0.16f, 0.83f, 0.02f, 1.0f));
            const ImU32 blue_bg = ImGui::GetColorU32(ImVec4(0.01f, 0.29f, 0.878f, 1.0f));

            changed |= DrawVec3Row("Position", "pos", position, 0.01f, red_bg, green_bg, blue_bg);
            rotation_changed = DrawVec3Row("Rotation", "rot", rotation, 0.25f, red_bg, green_bg, blue_bg);
            changed |= rotation_changed;
            changed |= DrawVec3Row("Scale", "scale", scale, 0.01f, red_bg, green_bg, blue_bg);

            ImGui::EndTable();
        }

        rotation_state.x = rotation[0];
        rotation_state.y = rotation[1];
        rotation_state.z = rotation[2];
        rotation_state.initialized = true;
        SaveRotationUiState(entity, rotation_state);

        if (!changed || commands == nullptr)
        {
            return;
        }

        core::scene::Transform updated = transform;
        updated.translation = glm::vec3(position[0], position[1], position[2]);
        if (reset_clicked)
        {
            updated.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
        else if (rotation_changed)
        {
            const auto delta_degrees = glm::vec3(rotation[0] - previous_rotation[0],
                                                      rotation[1] - previous_rotation[1],
                                                      rotation[2] - previous_rotation[2]);
            updated.rotation = ApplyEulerDeltaDegrees(transform.rotation, delta_degrees);
        }
        updated.scale = glm::vec3(scale[0], scale[1], scale[2]);
        EnqueueCommand(*commands, EntitySetLocalTransformCommand{entity, updated});
    }

} // namespace hybrid::ui
