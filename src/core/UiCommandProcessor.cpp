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

        glm::vec3 NormalizeOrFallback(const glm::vec3 &vector, const glm::vec3 &fallback)
        {
            const float magnitude_squared = glm::dot(vector, vector);
            if (magnitude_squared <= 1e-8f)
            {
                return fallback;
            }
            return glm::normalize(vector);
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
                    else if constexpr (std::is_same_v<T, ui::AddPointLightCommand>)
                    {
                        if (active_scene_world == nullptr)
                        {
                            return;
                        }

                        const entt::entity entity = active_scene_world->CreateEntity("Point Light");
                        auto &registry = active_scene_world->Registry();
                        registry.emplace<scene::LightCommonComponent>(entity);
                        registry.emplace<scene::PointLightComponent>(entity);
                    }
                    else if constexpr (std::is_same_v<T, ui::AddAreaLightCommand>)
                    {
                        if (active_scene_world == nullptr)
                        {
                            return;
                        }

                        const entt::entity entity = active_scene_world->CreateEntity("Area Light");
                        auto &registry = active_scene_world->Registry();
                        registry.emplace<scene::LightCommonComponent>(entity);
                        registry.emplace<scene::AreaLightComponent>(entity);
                    }
                    else if constexpr (std::is_same_v<T, ui::AddDirectionalLightCommand>)
                    {
                        if (active_scene_world == nullptr)
                        {
                            return;
                        }

                        const entt::entity entity = active_scene_world->CreateEntity("Directional Light");
                        auto &registry = active_scene_world->Registry();
                        registry.emplace<scene::LightCommonComponent>(entity);
                        registry.emplace<scene::DirectionalLightComponent>(entity);
                    }
                    else if constexpr (std::is_same_v<T, ui::EditLightCommonCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        auto &registry = active_scene_world->Registry();
                        if (auto *light = registry.try_get<scene::LightCommonComponent>(typed_command.entity))
                        {
                            light->color = glm::max(typed_command.color, glm::vec3(0.0f));
                            light->intensity = std::max(0.0f, typed_command.intensity);
                            light->cast_shadows = typed_command.cast_shadows;
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::EditPointLightCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        auto &registry = active_scene_world->Registry();
                        if (auto *light = registry.try_get<scene::PointLightComponent>(typed_command.entity))
                        {
                            light->range = std::max(0.0f, typed_command.range);
                            light->attenuation_constant = std::max(0.0f, typed_command.attenuation_constant);
                            light->attenuation_linear = std::max(0.0f, typed_command.attenuation_linear);
                            light->attenuation_quadratic = std::max(0.0f, typed_command.attenuation_quadratic);
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::EditDirectionalLightCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        auto &registry = active_scene_world->Registry();
                        if (auto *light = registry.try_get<scene::DirectionalLightComponent>(typed_command.entity))
                        {
                            light->direction = NormalizeOrFallback(typed_command.direction, glm::vec3(0.0f, -1.0f, 0.0f));
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::EditAreaLightCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        auto &registry = active_scene_world->Registry();
                        if (auto *light = registry.try_get<scene::AreaLightComponent>(typed_command.entity))
                        {
                            light->size = glm::max(typed_command.size, glm::vec2(0.0f));
                            light->direction = NormalizeOrFallback(typed_command.direction, glm::vec3(0.0f, -1.0f, 0.0f));
                            light->two_sided = typed_command.two_sided;
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::EditHdriLightCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        auto &registry = active_scene_world->Registry();
                        if (auto *light = registry.try_get<scene::HdriLightComponent>(typed_command.entity))
                        {
                            light->yaw_radians = typed_command.yaw_radians;
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
