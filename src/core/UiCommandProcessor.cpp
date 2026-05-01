#include "core/UiCommandProcessor.h"

#include "core/scene/SceneWorld.h"
#include "core/scene/types/SceneAssets.h"
#include "core/scene/types/SceneComponents.h"

#include <algorithm>
#include <type_traits>
#include <variant>
#include <vector>

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
                        active_scene_world->SetName(typed_command.entity, typed_command.name);
                    }
                    else if constexpr (std::is_same_v<T, ui::EntitySetLocalTransformCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }
                        active_scene_world->SetLocalTransform(typed_command.entity, typed_command.local);
                    }
                    else if constexpr (std::is_same_v<T, ui::CameraSetLensCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }
                        active_scene_world->SetCameraLens(typed_command.entity,
                                                          typed_command.horizontal_fov_radians,
                                                          typed_command.near_plane,
                                                          typed_command.far_plane);
                    }
                    else if constexpr (std::is_same_v<T, ui::CameraSetTargetCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }
                        active_scene_world->SetCameraTarget(typed_command.entity,
                                                            typed_command.enabled,
                                                            typed_command.target);
                    }
                    else if constexpr (std::is_same_v<T, ui::CameraSetPrimaryCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }
                        active_scene_world->SetPrimaryCamera(typed_command.entity, true);
                    }
                    else if constexpr (std::is_same_v<T, ui::AddCameraCommand>)
                    {
                        if (active_scene_world == nullptr)
                        {
                            return;
                        }

                        const entt::entity entity = active_scene_world->CreateEntity("Camera");
                        active_scene_world->AddCamera(entity);

                        std::vector<entt::entity> camera_entities{};
                        active_scene_world->GetEntitiesWithCamera(camera_entities);
                        bool has_primary_camera = false;
                        for (const entt::entity camera_entity : camera_entities)
                        {
                            if (active_scene_world->TryGetPrimaryCamera(camera_entity) != nullptr)
                            {
                                has_primary_camera = true;
                                break;
                            }
                        }

                        if (!has_primary_camera)
                        {
                            active_scene_world->SetPrimaryCamera(entity, true);
                        }
                    }
                    else if constexpr (std::is_same_v<T, ui::AddPointLightCommand>)
                    {
                        if (active_scene_world == nullptr)
                        {
                            return;
                        }

                        const entt::entity entity = active_scene_world->CreateEntity("Point Light");
                        active_scene_world->AddPointLight(entity, scene::LightCommonComponent{}, scene::PointLightComponent{});
                    }
                    else if constexpr (std::is_same_v<T, ui::AddAreaLightCommand>)
                    {
                        if (active_scene_world == nullptr)
                        {
                            return;
                        }

                        const entt::entity entity = active_scene_world->CreateEntity("Area Light");
                        active_scene_world->AddAreaLight(entity, scene::LightCommonComponent{}, scene::AreaLightComponent{});
                    }
                    else if constexpr (std::is_same_v<T, ui::AddDirectionalLightCommand>)
                    {
                        if (active_scene_world == nullptr)
                        {
                            return;
                        }

                        const entt::entity entity = active_scene_world->CreateEntity("Directional Light");
                        active_scene_world->AddDirectionalLight(entity,
                                                                scene::LightCommonComponent{},
                                                                scene::DirectionalLightComponent{});
                    }
                    else if constexpr (std::is_same_v<T, ui::EditLightCommonCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        scene::LightCommonComponent updated{};
                        updated.color = glm::max(typed_command.color, glm::vec3(0.0f));
                        updated.intensity = std::max(0.0f, typed_command.intensity);
                        updated.cast_shadows = typed_command.cast_shadows;
                        active_scene_world->SetLightCommon(typed_command.entity, updated);
                    }
                    else if constexpr (std::is_same_v<T, ui::EditPointLightCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        scene::PointLightComponent updated{};
                        updated.range = std::max(0.0f, typed_command.range);
                        updated.attenuation_constant = std::max(0.0f, typed_command.attenuation_constant);
                        updated.attenuation_linear = std::max(0.0f, typed_command.attenuation_linear);
                        updated.attenuation_quadratic = std::max(0.0f, typed_command.attenuation_quadratic);
                        active_scene_world->SetPointLight(typed_command.entity, updated);
                    }
                    else if constexpr (std::is_same_v<T, ui::EditAreaLightCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        scene::AreaLightComponent updated{};
                        updated.size = glm::max(typed_command.size, glm::vec2(0.0f));
                        updated.direction = NormalizeOrFallback(typed_command.direction, glm::vec3(0.0f, -1.0f, 0.0f));
                        updated.two_sided = typed_command.two_sided;
                        updated.visible = typed_command.visible;
                        active_scene_world->SetAreaLight(typed_command.entity, updated);
                    }
                    else if constexpr (std::is_same_v<T, ui::EditHdriLightCommand>)
                    {
                        if (active_scene_world == nullptr || !active_scene_world->IsValid(typed_command.entity))
                        {
                            return;
                        }

                        if (const scene::HdriLightComponent *existing = active_scene_world->TryGetHdriLight(typed_command.entity))
                        {
                            scene::HdriLightComponent updated = *existing;
                            updated.yaw_radians = typed_command.yaw_radians;
                            if (!typed_command.texture_path.empty())
                            {
                                updated.texture = assets.LoadHandle<assets::ImageAsset>(typed_command.texture_path);
                                if (updated.texture.IsValid())
                                {
                                    updated.texture_path = typed_command.texture_path;
                                }
                            }
                            active_scene_world->SetHdriLight(typed_command.entity, updated);
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
