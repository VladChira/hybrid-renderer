#pragma once

#include "SceneTypes.h"

#include <entt/entt.hpp>

namespace hybrid::core::scene
{

    class SceneWorld
    {
    public:
        entt::entity CreateEntity(const std::string &name = {});
        void DestroyEntity(entt::entity entity);
        bool IsValid(entt::entity entity) const;

        entt::registry &Registry() { return m_registry; }
        const entt::registry &Registry() const { return m_registry; }

        void SetParent(entt::entity child, entt::entity parent);
        entt::entity GetParent(entt::entity child) const;
        const std::vector<entt::entity> &GetChildren(entt::entity parent) const;

        void MarkDirty(entt::entity entity);
        void UpdateTransforms();

    private:
        void RemoveChild(entt::entity parent, entt::entity child);
        void MarkDirtyRecursive(entt::entity entity);
        void UpdateNodeRecursive(entt::entity entity, const glm::mat4 &parent_world, bool parent_dirty);
        bool IsAncestor(entt::entity ancestor, entt::entity entity) const;

        entt::registry m_registry;
        std::vector<entt::entity> m_empty_children;
    };

} // namespace hybrid::core::scene
