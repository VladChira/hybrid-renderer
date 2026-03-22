#include "core/scene/SceneCameraResolver.h"

#include "core/Log.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace hybrid::core::scene
{

    namespace
    {
        constexpr glm::vec3 kDefaultCameraPosition(-3.0f, 2.76f, 0.24f);

        float ResolveAspect(float requested_aspect, float fallback_aspect)
        {
            if (requested_aspect > 0.0f)
            {
                return requested_aspect;
            }
            if (fallback_aspect > 0.0f)
            {
                return fallback_aspect;
            }
            return 16.0f / 9.0f;
        }

        float HorizontalToVerticalFov(float horizontal_fov_radians, float aspect_ratio)
        {
            const float safe_aspect = std::max(aspect_ratio, 1e-4f);
            const float half_h = 0.5f * horizontal_fov_radians;
            return 2.0f * std::atan(std::tan(half_h) / safe_aspect);
        }

        entt::entity EnsureDefaultCameraEntity(SceneWorld &scene_world)
        {
            auto &registry = scene_world.Registry();
            if (auto camera_view = registry.view<CameraComponent, TransformComponent>(); camera_view.begin() != camera_view.end())
            {
                return *camera_view.begin();
            }

            if (static bool warned = false; !warned)
            {
                LOG_WARN("[SceneCameraResolver] No camera found in scene. Using DEFAULT camera.");
                warned = true;
            }
            

            const entt::entity camera_entity = scene_world.CreateEntity("Default Camera");
            auto &transform = registry.get<TransformComponent>(camera_entity);
            transform.local.translation = kDefaultCameraPosition;
            transform.local.rotation = glm::quatLookAt(glm::normalize(-kDefaultCameraPosition),
                                                       glm::vec3(0.0f, 1.0f, 0.0f));
            transform.local.scale = glm::vec3(1.0f);
            transform.dirty = true;

            auto &camera = registry.emplace<CameraComponent>(camera_entity);
            camera.horizontal_fov_radians = glm::radians(60.0f);
            camera.near_plane = 0.1f;
            camera.far_plane = 1000.0f;

            scene_world.UpdateTransforms();
            return camera_entity;
        }

        SceneCameraView BuildCameraView(const SceneWorld &scene_world, entt::entity camera_entity, float aspect_ratio)
        {
            SceneCameraView view{};
            const auto &registry = scene_world.Registry();
            const auto *camera = registry.try_get<CameraComponent>(camera_entity);
            const auto *transform = registry.try_get<TransformComponent>(camera_entity);
            if (camera == nullptr || transform == nullptr)
            {
                return view;
            }

            const float vertical_fov = HorizontalToVerticalFov(camera->horizontal_fov_radians, aspect_ratio);

            const auto position = glm::vec3(transform->world[3]);
            glm::vec3 forward = glm::normalize(glm::vec3(transform->world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
            glm::vec3 up = glm::normalize(glm::vec3(transform->world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));

            if (const auto *target = registry.try_get<CameraTargetComponent>(camera_entity);
                target != nullptr && target->enabled && registry.valid(target->target))
            {
                if (const auto *target_transform = registry.try_get<TransformComponent>(target->target))
                {
                    const auto target_position = glm::vec3(target_transform->world[3]);
                    const glm::vec3 to_target = target_position - position;
                    if (glm::dot(to_target, to_target) > 1e-6f)
                    {
                        forward = glm::normalize(to_target);
                    }
                }
            }

            view.position = position;
            view.near_plane = camera->near_plane;
            view.far_plane = camera->far_plane;
            view.view = glm::lookAt(position, position + forward, up);
            view.projection = glm::perspective(vertical_fov, aspect_ratio, view.near_plane, view.far_plane);
            view.valid = true;
            return view;
        }
    } // namespace

    SceneCameraView ResolvePrimaryCameraView(SceneWorld &scene_world, float aspect_ratio)
    {
        const entt::entity primary_camera = EnsureDefaultCameraEntity(scene_world);
        return BuildCameraView(scene_world, primary_camera, aspect_ratio);
    }

} // namespace hybrid::core::scene
