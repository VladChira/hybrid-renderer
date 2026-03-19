#pragma once

#include "assets/AssetManager.h"
#include "core/scene/types/SceneMath.h"

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
        float horizontal_fov_radians = 1.0471976f; // 60 degrees
        float aspect_ratio = 16.0f / 9.0f;
        float near_plane = 0.1f;
        float far_plane = 1000.0f;
    };

    struct CameraTargetComponent
    {
        bool enabled = false;
        entt::entity target = entt::null;
    };

} // namespace hybrid::core::scene
