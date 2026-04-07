#include "SceneHierarchyPanel.h"

#include "core/Profiling.h"
#include "core/scene/SceneWorld.h"
#include "core/scene/types/SceneComponents.h"
#include "ui/UiState.h"

#include <imgui.h>

#include <entt/entt.hpp>

#include <cstdint>
#include <string>

namespace hybrid::ui
{
    namespace
    {
        std::string BuildEntityLabel(const core::scene::SceneWorld &scene_world, entt::entity entity)
        {
            const auto &registry = scene_world.Registry();
            if (const auto *name = registry.try_get<core::scene::NameComponent>(entity))
            {
                if (!name->name.empty())
                {
                    return name->name;
                }
            }

            const auto id = entt::to_integral(entity);
            return "Entity " + std::to_string(id);
        }

        void DrawEntityTree(const core::scene::SceneWorld &scene_world,
                            entt::entity entity,
                            UiSelection *selection,
                            CommandBuffer *commands)
        {
            if (!scene_world.IsValid(entity))
            {
                return;
            }

            const auto &registry = scene_world.Registry();
            const auto &children = scene_world.GetChildren(entity);
            const bool has_children = !children.empty();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
            if (selection &&
                selection->type == UiSelection::Type::Entity &&
                selection->entity == entity)
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (!has_children)
            {
                flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            }

            ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
            const std::string label = BuildEntityLabel(scene_world, entity);

            const bool open = ImGui::TreeNodeEx(label.c_str(), flags);

            if (ImGui::IsItemClicked() && selection)
            {
                selection->type = UiSelection::Type::Entity;
                selection->entity = entity;
                selection->material_asset_id = 0;
            }

            const bool is_camera = registry.all_of<core::scene::CameraComponent>(entity);
            if (is_camera)
            {
                const bool is_primary = registry.all_of<core::scene::PrimaryCameraComponent>(entity);
                const float toggle_size = ImGui::GetFrameHeight();
                const float toggle_x = ImGui::GetWindowContentRegionMax().x - toggle_size - 4.0f;
                ImGui::SameLine(toggle_x);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));
                if (ImGui::RadioButton("##primary_camera", is_primary))
                {
                    if (!is_primary && commands != nullptr)
                    {
                        EnqueueCommand(*commands, CameraSetPrimaryCommand{entity});
                    }
                    if (selection)
                    {
                        selection->type = UiSelection::Type::Entity;
                        selection->entity = entity;
                        selection->material_asset_id = 0;
                    }
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Set as primary viewport camera");
                }
                ImGui::PopStyleVar();
            }

            if (has_children && open)
            {
                for (const entt::entity child : children)
                {
                    DrawEntityTree(scene_world, child, selection, commands);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    } // namespace

    SceneHierarchyPanel::SceneHierarchyPanel()
        : Panel("Scene Hierarchy")
    {
    }

    void SceneHierarchyPanel::DrawContents(PanelContext &context)
    {
        HYBRID_PROFILE_ZONE_N("SceneHierarchyPanel::DrawContents");
        const UiState *state = context.state;
        if (!state || !state->scene_world)
        {
            ImGui::TextUnformatted("No scene loaded.");
            return;
        }

        const core::scene::SceneWorld &scene_world = *state->scene_world;
        const auto &registry = scene_world.Registry();
        auto view = registry.view<core::scene::HierarchyComponent>();

        bool drew_any = false;

        for (const entt::entity entity : view)
        {
            if (const auto &hierarchy = view.get<core::scene::HierarchyComponent>(entity); hierarchy.parent != entt::null)
            {
                continue;
            }

            drew_any = true;
            DrawEntityTree(scene_world, entity, context.selection, context.commands);
        }

        if (!drew_any)
        {
            ImGui::TextUnformatted("Scene is empty.");
        }
    }

} // namespace hybrid::ui
