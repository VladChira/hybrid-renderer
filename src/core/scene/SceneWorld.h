#pragma once

#include "SceneTypes.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace hybrid::core::scene
{

    class SceneWorld
    {
    public:
        struct RenderDirtyQueues
        {
            std::vector<entt::entity> transform_entities;
            std::vector<entt::entity> mesh_entities;
            std::vector<entt::entity> light_entities;
            std::vector<entt::entity> hierarchy_entities;
            std::vector<entt::entity> destroyed_entities;
            bool structure_changed = false;
        };

        SceneWorld();
        ~SceneWorld();

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

        RenderDirtyQueues ConsumeRenderDirtyQueues();

    private:
        enum class DirtyQueueKind : uint8_t
        {
            Transform,
            Mesh,
            Light,
            Hierarchy,
            Destroyed
        };

        void EnqueueDirty(DirtyQueueKind kind, entt::entity entity);
        void MarkStructureChanged();
        void ResetDirtyDedup();

        void OnConstructTransform(entt::registry &registry, entt::entity entity);
        void OnUpdateTransform(entt::registry &registry, entt::entity entity);
        void OnDestroyTransform(entt::registry &registry, entt::entity entity);

        void OnConstructHierarchy(entt::registry &registry, entt::entity entity);
        void OnUpdateHierarchy(entt::registry &registry, entt::entity entity);
        void OnDestroyHierarchy(entt::registry &registry, entt::entity entity);

        void OnConstructMeshRenderer(entt::registry &registry, entt::entity entity);
        void OnUpdateMeshRenderer(entt::registry &registry, entt::entity entity);
        void OnDestroyMeshRenderer(entt::registry &registry, entt::entity entity);

        void OnConstructLightCommon(entt::registry &registry, entt::entity entity);
        void OnUpdateLightCommon(entt::registry &registry, entt::entity entity);
        void OnDestroyLightCommon(entt::registry &registry, entt::entity entity);

        void OnConstructDirectionalLight(entt::registry &registry, entt::entity entity);
        void OnUpdateDirectionalLight(entt::registry &registry, entt::entity entity);
        void OnDestroyDirectionalLight(entt::registry &registry, entt::entity entity);

        void OnConstructPointLight(entt::registry &registry, entt::entity entity);
        void OnUpdatePointLight(entt::registry &registry, entt::entity entity);
        void OnDestroyPointLight(entt::registry &registry, entt::entity entity);

        void OnConstructAreaLight(entt::registry &registry, entt::entity entity);
        void OnUpdateAreaLight(entt::registry &registry, entt::entity entity);
        void OnDestroyAreaLight(entt::registry &registry, entt::entity entity);

        void OnConstructHdriLight(entt::registry &registry, entt::entity entity);
        void OnUpdateHdriLight(entt::registry &registry, entt::entity entity);
        void OnDestroyHdriLight(entt::registry &registry, entt::entity entity);

        void RemoveChild(entt::entity parent, entt::entity child);
        void MarkDirtyRecursive(entt::entity entity);
        void UpdateNodeRecursive(entt::entity entity, const glm::mat4 &parent_world, bool parent_dirty);
        bool IsAncestor(entt::entity ancestor, entt::entity entity) const;

        entt::registry m_registry;
        std::vector<entt::entity> m_empty_children;
        RenderDirtyQueues m_render_dirty_queues;
        std::unordered_set<uint32_t> m_transform_dedup;
        std::unordered_set<uint32_t> m_mesh_dedup;
        std::unordered_set<uint32_t> m_light_dedup;
        std::unordered_set<uint32_t> m_hierarchy_dedup;
        std::unordered_set<uint32_t> m_destroyed_dedup;
    };

} // namespace hybrid::core::scene
