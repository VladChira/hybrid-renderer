#pragma once

#include "assets/AssetManager.h"
#include "core/scene/types/SceneMath.h"
#include "assets/ImageAsset.h"

#include <string>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace hybrid::core::scene
{

    struct MeshAsset;

    struct NameComponent
    {
        std::string name;
    };

    struct TransformComponent
    {
        Transform local{};
        glm::mat4 world{1.0f};
        bool dirty = true;
    };

    struct HierarchyComponent
    {
        entt::entity parent{entt::null};
        std::vector<entt::entity> children;
    };

    struct MeshRendererComponent
    {
        assets::AssetHandle<MeshAsset> mesh;
    };

    struct CameraComponent
    {
        float horizontal_fov_radians = 1.308996939f; // 75 degrees
        float near_plane = 0.1f;
        float far_plane = 1000.0f;
    };

    struct PrimaryCameraComponent
    {
    };

    struct CameraTargetComponent
    {
        bool enabled = false;
        entt::entity target = entt::null;
    };

    struct LightCommonComponent
    {
        glm::vec3 color{1.0f};
        float intensity = 10.0f;
        bool cast_shadows = true;
    };

    struct PointLightComponent
    {
        // 0.0 means unspecified/infinite range.
        float range = 0.0f;
        float attenuation_constant = 0.0f;
        float attenuation_linear = 0.0f;
        float attenuation_quadratic = 1.0f;
    };

    struct DirectionalLightComponent
    {
        // Direction is computed based on local transform
    };

    struct AreaLightComponent
    {
        // Extents in local X/Y.
        glm::vec2 size{1.0f, 1.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        bool two_sided = false;
    };

    struct HdriLightComponent
    {
        // 0.0 means no explicit rotation override.
        float yaw_radians = 0.0f;
        assets::AssetHandle<assets::ImageAsset> texture;
    };

} // namespace hybrid::core::scene
