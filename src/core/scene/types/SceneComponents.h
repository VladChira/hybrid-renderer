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

} // namespace hybrid::core::scene
