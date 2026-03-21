#include "PropertiesPanel.h"

#include "core/scene/SceneWorld.h"
#include "core/scene/types/SceneComponents.h"
#include "ui/UiState.h"
#include "ui/panels/components/CameraComponentDrawer.h"
#include "ui/panels/components/CameraTargetComponentDrawer.h"
#include "ui/panels/components/MaterialComponentDrawer.h"
#include "ui/panels/components/MeshRendererComponentDrawer.h"
#include "ui/panels/components/NameComponentDrawer.h"
#include "ui/panels/components/TransformComponentDrawer.h"

#include <imgui.h>

namespace hybrid::ui
{
    namespace
    {
        const core::scene::MaterialAsset *FindSelectedMaterial(const UiState &state, uint64_t material_asset_id)
        {
            for (const UiMaterialEntry &entry : state.materials)
            {
                if (entry.asset_id == material_asset_id)
                {
                    return entry.material;
                }
            }

            return nullptr;
        }
    } // namespace

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

        if (!context.selection || context.selection->type == UiSelection::Type::None)
        {
            ImGui::TextUnformatted("Select an entity or material to see properties.");
            return;
        }

        if (context.selection->type == UiSelection::Type::Material)
        {
            const core::scene::MaterialAsset *material = FindSelectedMaterial(*state, context.selection->material_asset_id);
            if (!material)
            {
                ImGui::TextUnformatted("Selected material is unavailable.");
                return;
            }

            DrawMaterialComponent(*material, context.selection->material_asset_id, context.commands);
            return;
        }

        if (context.selection->type != UiSelection::Type::Entity)
        {
            ImGui::TextUnformatted("Unsupported selection type.");
            return;
        }

        const core::scene::SceneWorld &scene_world = *state->scene_world;
        const entt::entity entity = context.selection->entity;
        if (!scene_world.IsValid(entity))
        {
            ImGui::TextUnformatted("Selection is invalid for this scene.");
            return;
        }

        const auto &registry = scene_world.Registry();
        bool drew_any = false;

        if (const auto *name = registry.try_get<core::scene::NameComponent>(entity))
        {
            DrawNameComponent(entity, *name, context.commands);
            drew_any = true;
        }

        if (const auto *transform = registry.try_get<core::scene::TransformComponent>(entity))
        {
            DrawTransformComponent(entity, *transform, context.commands);
            drew_any = true;
        }

        if (const auto *mesh = registry.try_get<core::scene::MeshRendererComponent>(entity))
        {
            DrawMeshRendererComponent(*mesh);
            drew_any = true;
        }

        if (const auto *camera = registry.try_get<core::scene::CameraComponent>(entity))
        {
            DrawCameraComponent(entity, *camera, context.commands);
            drew_any = true;
        }

        if (const auto *camera_target = registry.try_get<core::scene::CameraTargetComponent>(entity))
        {
            DrawCameraTargetComponent(entity, *camera_target, context.commands);
            drew_any = true;
        }

        if (!drew_any)
        {
            ImGui::TextUnformatted("No components to display.");
        }
    }

} // namespace hybrid::ui
