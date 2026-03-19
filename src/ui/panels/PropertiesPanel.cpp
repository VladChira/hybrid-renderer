#include "PropertiesPanel.h"

#include "core/scene/SceneWorld.h"
#include "core/scene/types/SceneComponents.h"
#include "ui/UiState.h"
#include "ui/panels/components/CameraComponentDrawer.h"
#include "ui/panels/components/CameraTargetComponentDrawer.h"
#include "ui/panels/components/MeshRendererComponentDrawer.h"
#include "ui/panels/components/NameComponentDrawer.h"
#include "ui/panels/components/TransformComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{

    PropertiesPanel::PropertiesPanel()
        : Panel("Properties")
    {
    }

    void PropertiesPanel::DrawContents(PanelContext &context)
    {
        const UiState *state = context.state;
        if (!state || !state->scene_world)
        {
            ImGui::TextUnformatted("No scene loaded.");
            return;
        }

        if (!context.selected_entity || *context.selected_entity == entt::null)
        {
            ImGui::TextUnformatted("Select an entity to see properties.");
            return;
        }

        const core::scene::SceneWorld &scene_world = *state->scene_world;
        const entt::entity entity = *context.selected_entity;
        if (!scene_world.IsValid(entity))
        {
            ImGui::TextUnformatted("Selection is invalid for this scene.");
            return;
        }

        const auto &registry = scene_world.Registry();
        bool drew_any = false;

        if (const auto *name = registry.try_get<core::scene::NameComponent>(entity))
        {
            DrawNameComponent(*name);
            drew_any = true;
        }

        if (const auto *transform = registry.try_get<core::scene::TransformComponent>(entity))
        {
            DrawTransformComponent(*transform);
            drew_any = true;
        }

        if (const auto *mesh = registry.try_get<core::scene::MeshRendererComponent>(entity))
        {
            DrawMeshRendererComponent(*mesh);
            drew_any = true;
        }

        if (const auto *camera = registry.try_get<core::scene::CameraComponent>(entity))
        {
            DrawCameraComponent(*camera);
            drew_any = true;
        }

        if (const auto *camera_target = registry.try_get<core::scene::CameraTargetComponent>(entity))
        {
            DrawCameraTargetComponent(*camera_target);
            drew_any = true;
        }

        if (!drew_any)
        {
            ImGui::TextUnformatted("No components to display.");
        }
    }

} // namespace hybrid::ui
