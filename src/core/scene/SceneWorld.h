#pragma once

#include "SceneReadApi.h"
#include "SceneWriteApi.h"
#include "SceneTypes.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace hybrid::core::scene
{

    class SceneWorld : public ISceneReadApi, public ISceneWriteApi
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

        entt::entity CreateEntity(const std::string &name = {}) override;
        bool DestroyEntity(entt::entity entity) override;
        bool SetName(entt::entity entity, const std::string &name) override;
        bool IsValid(entt::entity entity) const override;

        const NameComponent *TryGetName(entt::entity entity) const override;
        const TransformComponent *TryGetTransform(entt::entity entity) const override;
        const HierarchyComponent *TryGetHierarchy(entt::entity entity) const override;
        const MeshRendererComponent *TryGetMeshRenderer(entt::entity entity) const override;
        const CameraComponent *TryGetCamera(entt::entity entity) const override;
        const PrimaryCameraComponent *TryGetPrimaryCamera(entt::entity entity) const override;
        const CameraTargetComponent *TryGetCameraTarget(entt::entity entity) const override;
        const LightCommonComponent *TryGetLightCommon(entt::entity entity) const override;
        const DirectionalLightComponent *TryGetDirectionalLight(entt::entity entity) const override;
        const PointLightComponent *TryGetPointLight(entt::entity entity) const override;
        const AreaLightComponent *TryGetAreaLight(entt::entity entity) const override;
        const HdriLightComponent *TryGetHdriLight(entt::entity entity) const override;

        const entt::registry &Registry() const { return m_registry; }

        bool SetLocalTransform(entt::entity entity, const Transform &local) override;
        bool SetLocalTranslation(entt::entity entity, const glm::vec3 &translation) override;
        bool SetLocalRotation(entt::entity entity, const glm::quat &rotation) override;
        bool SetLocalScale(entt::entity entity, const glm::vec3 &scale) override;

        bool SetParent(entt::entity child, entt::entity parent) override;
        bool ClearParent(entt::entity child) override;
        entt::entity GetParent(entt::entity child) const override;
        const std::vector<entt::entity> &GetChildren(entt::entity parent) const override;

        bool AddCamera(entt::entity entity, const CameraComponent &camera = {}) override;
        bool RemoveCamera(entt::entity entity) override;
        bool SetCameraLens(entt::entity entity,
                           float horizontal_fov_radians,
                           float near_plane,
                           float far_plane) override;
        bool SetPrimaryCamera(entt::entity entity, bool is_primary) override;
        bool SetCameraTarget(entt::entity entity, bool enabled, entt::entity target) override;

        bool AddMeshRenderer(entt::entity entity, const MeshRendererComponent &mesh_renderer) override;
        bool RemoveMeshRenderer(entt::entity entity) override;
        bool SetMeshRenderer(entt::entity entity, const MeshRendererComponent &mesh_renderer) override;

        bool AddDirectionalLight(entt::entity entity,
                                 const LightCommonComponent &common,
                                 const DirectionalLightComponent &directional = {}) override;
        bool AddPointLight(entt::entity entity,
                           const LightCommonComponent &common,
                           const PointLightComponent &point = {}) override;
        bool AddAreaLight(entt::entity entity,
                          const LightCommonComponent &common,
                          const AreaLightComponent &area = {}) override;
        bool AddHdriLight(entt::entity entity,
                          const LightCommonComponent &common,
                          const HdriLightComponent &hdri = {}) override;
        bool RemoveLight(entt::entity entity) override;
        bool SetLightCommon(entt::entity entity, const LightCommonComponent &common) override;
        bool SetPointLight(entt::entity entity, const PointLightComponent &point) override;
        bool SetAreaLight(entt::entity entity, const AreaLightComponent &area) override;
        bool SetHdriLight(entt::entity entity, const HdriLightComponent &hdri) override;

        uint32_t GetEntityCount() const override;
        void GetEntities(std::vector<entt::entity> &out_entities) const override;
        void GetEntitiesWithTransform(std::vector<entt::entity> &out_entities) const override;
        void GetEntitiesWithMeshRenderer(std::vector<entt::entity> &out_entities) const override;
        void GetEntitiesWithCamera(std::vector<entt::entity> &out_entities) const override;
        void GetEntitiesWithLight(std::vector<entt::entity> &out_entities) const override;

        void MarkDirty(entt::entity entity) override;
        void FlushPendingChanges() override;
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
