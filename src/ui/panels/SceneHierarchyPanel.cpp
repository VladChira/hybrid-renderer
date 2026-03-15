#include "SceneHierarchyPanel.h"

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

            const auto id = static_cast<uint32_t>(entt::to_integral(entity));
            return "Entity " + std::to_string(id);
        }

        void DrawEntityTree(const core::scene::SceneWorld &scene_world,
                            entt::entity entity,
                            entt::entity *selected_entity)
        {
            if (!scene_world.IsValid(entity))
            {
                return;
            }

            const auto &children = scene_world.GetChildren(entity);
            const bool has_children = !children.empty();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selected_entity && *selected_entity == entity)
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

            if (ImGui::IsItemClicked() && selected_entity)
            {
                *selected_entity = entity;
            }

            if (has_children && open)
            {
                for (const entt::entity child : children)
                {
                    DrawEntityTree(scene_world, child, selected_entity);
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
            const auto &hierarchy = view.get<core::scene::HierarchyComponent>(entity);
            if (hierarchy.parent != entt::null)
            {
                continue;
            }

            drew_any = true;
            DrawEntityTree(scene_world, entity, context.selected_entity);
        }

        if (!drew_any)
        {
            ImGui::TextUnformatted("Scene is empty.");
        }
    }

} // namespace hybrid::ui
