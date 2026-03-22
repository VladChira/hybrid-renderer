#include "core/UiCommandProcessor.h"

#include "core/scene/SceneWorld.h"
#include "core/scene/types/SceneAssets.h"
#include "core/scene/types/SceneComponents.h"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace hybrid::core
{
    namespace
    {
        scene::SceneWorld *ResolveActiveSceneWorld(assets::AssetManager &assets, assets::AssetId active_scene)
        {
            if (!active_scene.IsValid())
            {
                return nullptr;
            }
            return assets.Get<scene::SceneWorld>(active_scene);
        }
    } // namespace

    void ProcessUiCommands(const ui::CommandBuffer &commands,
                           assets::AssetManager &assets,
                           assets::AssetId active_scene,
                           bool &should_quit)
    {
        scene::SceneWorld *active_scene_world = ResolveActiveSceneWorld(assets, active_scene);

        for (const auto &command : commands)
        {
            std::visit(
                [&](const auto &typed_command)
                {
                    using T = std::decay_t<decltype(typed_command)>;

                    if constexpr (std::is_same_v<T, ui::QuitCommand>)
                    {
                        should_quit = true;
                    }
                    else if constexpr (std::is_same_v<T, ui::EntityRenameCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        auto &registry = active_scene_world->Registry();
                        auto &name = registry.get_or_emplace<scene::NameComponent>(typed_command.entity);
                        name.name = typed_command.name;
                    }
                    else if constexpr (std::is_same_v<T, ui::EntitySetLocalTransformCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        auto &registry = active_scene_world->Registry();
                        if (auto *transform = registry.try_get<scene::TransformComponent>(typed_command.entity))
                        {
                            transform->local = typed_command.local;
                            active_scene_world->MarkDirty(typed_command.entity);
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::CameraSetLensCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        auto &registry = active_scene_world->Registry();
                        if (auto *camera = registry.try_get<scene::CameraComponent>(typed_command.entity))
                        {
                            camera->horizontal_fov_radians = std::max(0.0174533f, typed_command.horizontal_fov_radians);
                            camera->near_plane = std::max(0.001f, typed_command.near_plane);
                            camera->far_plane = std::max(camera->near_plane + 0.001f, typed_command.far_plane);
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::CameraSetTargetCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        auto &registry = active_scene_world->Registry();
                        if (auto *camera_target = registry.try_get<scene::CameraTargetComponent>(typed_command.entity))
                        {
                            camera_target->enabled = typed_command.enabled;
                            camera_target->target = typed_command.target;
                            if (camera_target->target != entt::null && !active_scene_world->IsValid(camera_target->target))
                            {
                                camera_target->target = entt::null;
                            }
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::MaterialSetScalarCommand>)
                    {
                        auto *material = assets.Get<scene::MaterialAsset>(assets::AssetId{typed_command.material_asset_id});
                        if (material == nullptr)
                        {
                            return;
                        }

                        switch (typed_command.property)
                        {
                        case ui::MaterialScalarProperty::MetallicFactor:
                            material->metallic_factor = std::clamp(typed_command.value, 0.0f, 1.0f);
                            break;
                        case ui::MaterialScalarProperty::RoughnessFactor:
                            material->roughness_factor = std::clamp(typed_command.value, 0.0f, 1.0f);
                            break;
                        case ui::MaterialScalarProperty::AlphaCutoff:
                            material->alpha_cutoff = std::clamp(typed_command.value, 0.0f, 1.0f);
                            break;
                        case ui::MaterialScalarProperty::NormalScale:
                            material->normal_scale = std::max(0.0f, typed_command.value);
                            break;
                        case ui::MaterialScalarProperty::OcclusionStrength:
                            material->occlusion_strength = std::clamp(typed_command.value, 0.0f, 1.0f);
                            break;
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::MaterialSetVec4Command>)
                    {
                        auto *material = assets.Get<scene::MaterialAsset>(assets::AssetId{typed_command.material_asset_id});
                        if (material == nullptr)
                        {
                            return;
                        }

                        switch (typed_command.property)
                        {
                        case ui::MaterialVec4Property::BaseColorFactor:
                            material->base_color_factor = typed_command.value;
                            break;
                        default:
                            break;
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::MaterialSetVec3Command>)
                    {
                        auto *material = assets.Get<scene::MaterialAsset>(assets::AssetId{typed_command.material_asset_id});
                        if (material == nullptr)
                        {
                            return;
                        }

                        switch (typed_command.property)
                        {
                        case ui::MaterialVec3Property::EmissiveFactor:
                            material->emissive_factor = typed_command.value;
                            break;
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::EditorCameraNavigateCommand>)
                    {
                        // Editor camera controls are intentionally decoupled from render passes.
                        // A dedicated editor-camera state will consume these commands.
                    }
                },
                command);
        }
    }

} // namespace hybrid::core
