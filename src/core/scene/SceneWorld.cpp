#include "core/scene/SceneWorld.h"

#include "core/Log.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

namespace hybrid::core::scene
{

    namespace
    {
        uint32_t EntityKey(const entt::entity entity)
        {
            return static_cast<uint32_t>(entt::to_integral(entity));
        }

        glm::mat4 ComposeMatrix(const Transform &transform)
        {
            glm::mat4 result(1.0f);
            result = glm::translate(result, transform.translation);
            result *= glm::mat4_cast(transform.rotation);
            result = glm::scale(result, transform.scale);
            return result;
        }

        template <typename TComponent>
        void EmitComponentUpdated(entt::registry &registry, const entt::entity entity)
        {
            registry.patch<TComponent>(entity, [](TComponent &) {});
        }
    } // namespace

    SceneWorld::SceneWorld()
    {
        m_registry.on_construct<TransformComponent>().connect<&SceneWorld::OnConstructTransform>(*this);
        m_registry.on_update<TransformComponent>().connect<&SceneWorld::OnUpdateTransform>(*this);
        m_registry.on_destroy<TransformComponent>().connect<&SceneWorld::OnDestroyTransform>(*this);

        m_registry.on_construct<HierarchyComponent>().connect<&SceneWorld::OnConstructHierarchy>(*this);
        m_registry.on_update<HierarchyComponent>().connect<&SceneWorld::OnUpdateHierarchy>(*this);
        m_registry.on_destroy<HierarchyComponent>().connect<&SceneWorld::OnDestroyHierarchy>(*this);

        m_registry.on_construct<MeshRendererComponent>().connect<&SceneWorld::OnConstructMeshRenderer>(*this);
        m_registry.on_update<MeshRendererComponent>().connect<&SceneWorld::OnUpdateMeshRenderer>(*this);
        m_registry.on_destroy<MeshRendererComponent>().connect<&SceneWorld::OnDestroyMeshRenderer>(*this);

        m_registry.on_construct<LightCommonComponent>().connect<&SceneWorld::OnConstructLightCommon>(*this);
        m_registry.on_update<LightCommonComponent>().connect<&SceneWorld::OnUpdateLightCommon>(*this);
        m_registry.on_destroy<LightCommonComponent>().connect<&SceneWorld::OnDestroyLightCommon>(*this);

        m_registry.on_construct<DirectionalLightComponent>().connect<&SceneWorld::OnConstructDirectionalLight>(*this);
        m_registry.on_update<DirectionalLightComponent>().connect<&SceneWorld::OnUpdateDirectionalLight>(*this);
        m_registry.on_destroy<DirectionalLightComponent>().connect<&SceneWorld::OnDestroyDirectionalLight>(*this);

        m_registry.on_construct<PointLightComponent>().connect<&SceneWorld::OnConstructPointLight>(*this);
        m_registry.on_update<PointLightComponent>().connect<&SceneWorld::OnUpdatePointLight>(*this);
        m_registry.on_destroy<PointLightComponent>().connect<&SceneWorld::OnDestroyPointLight>(*this);

        m_registry.on_construct<AreaLightComponent>().connect<&SceneWorld::OnConstructAreaLight>(*this);
        m_registry.on_update<AreaLightComponent>().connect<&SceneWorld::OnUpdateAreaLight>(*this);
        m_registry.on_destroy<AreaLightComponent>().connect<&SceneWorld::OnDestroyAreaLight>(*this);

        m_registry.on_construct<HdriLightComponent>().connect<&SceneWorld::OnConstructHdriLight>(*this);
        m_registry.on_update<HdriLightComponent>().connect<&SceneWorld::OnUpdateHdriLight>(*this);
        m_registry.on_destroy<HdriLightComponent>().connect<&SceneWorld::OnDestroyHdriLight>(*this);
    }

    SceneWorld::~SceneWorld()
    {
        m_registry.on_construct<TransformComponent>().disconnect<&SceneWorld::OnConstructTransform>(*this);
        m_registry.on_update<TransformComponent>().disconnect<&SceneWorld::OnUpdateTransform>(*this);
        m_registry.on_destroy<TransformComponent>().disconnect<&SceneWorld::OnDestroyTransform>(*this);

        m_registry.on_construct<HierarchyComponent>().disconnect<&SceneWorld::OnConstructHierarchy>(*this);
        m_registry.on_update<HierarchyComponent>().disconnect<&SceneWorld::OnUpdateHierarchy>(*this);
        m_registry.on_destroy<HierarchyComponent>().disconnect<&SceneWorld::OnDestroyHierarchy>(*this);

        m_registry.on_construct<MeshRendererComponent>().disconnect<&SceneWorld::OnConstructMeshRenderer>(*this);
        m_registry.on_update<MeshRendererComponent>().disconnect<&SceneWorld::OnUpdateMeshRenderer>(*this);
        m_registry.on_destroy<MeshRendererComponent>().disconnect<&SceneWorld::OnDestroyMeshRenderer>(*this);

        m_registry.on_construct<LightCommonComponent>().disconnect<&SceneWorld::OnConstructLightCommon>(*this);
        m_registry.on_update<LightCommonComponent>().disconnect<&SceneWorld::OnUpdateLightCommon>(*this);
        m_registry.on_destroy<LightCommonComponent>().disconnect<&SceneWorld::OnDestroyLightCommon>(*this);

        m_registry.on_construct<DirectionalLightComponent>().disconnect<&SceneWorld::OnConstructDirectionalLight>(*this);
        m_registry.on_update<DirectionalLightComponent>().disconnect<&SceneWorld::OnUpdateDirectionalLight>(*this);
        m_registry.on_destroy<DirectionalLightComponent>().disconnect<&SceneWorld::OnDestroyDirectionalLight>(*this);

        m_registry.on_construct<PointLightComponent>().disconnect<&SceneWorld::OnConstructPointLight>(*this);
        m_registry.on_update<PointLightComponent>().disconnect<&SceneWorld::OnUpdatePointLight>(*this);
        m_registry.on_destroy<PointLightComponent>().disconnect<&SceneWorld::OnDestroyPointLight>(*this);

        m_registry.on_construct<AreaLightComponent>().disconnect<&SceneWorld::OnConstructAreaLight>(*this);
        m_registry.on_update<AreaLightComponent>().disconnect<&SceneWorld::OnUpdateAreaLight>(*this);
        m_registry.on_destroy<AreaLightComponent>().disconnect<&SceneWorld::OnDestroyAreaLight>(*this);

        m_registry.on_construct<HdriLightComponent>().disconnect<&SceneWorld::OnConstructHdriLight>(*this);
        m_registry.on_update<HdriLightComponent>().disconnect<&SceneWorld::OnUpdateHdriLight>(*this);
        m_registry.on_destroy<HdriLightComponent>().disconnect<&SceneWorld::OnDestroyHdriLight>(*this);
    }

    SceneWorld::RenderDirtyQueues SceneWorld::ConsumeRenderDirtyQueues()
    {
        RenderDirtyQueues consumed{};
        consumed.transform_entities = std::move(m_render_dirty_queues.transform_entities);
        consumed.mesh_entities = std::move(m_render_dirty_queues.mesh_entities);
        consumed.light_entities = std::move(m_render_dirty_queues.light_entities);
        consumed.hierarchy_entities = std::move(m_render_dirty_queues.hierarchy_entities);
        consumed.destroyed_entities = std::move(m_render_dirty_queues.destroyed_entities);
        consumed.structure_changed = m_render_dirty_queues.structure_changed;

        m_render_dirty_queues = {};
        ResetDirtyDedup();
        return consumed;
    }

    entt::entity SceneWorld::CreateEntity(const std::string &name)
    {
        entt::entity entity = m_registry.create();
        if (!name.empty())
        {
            m_registry.emplace<NameComponent>(entity, NameComponent{name});
        }
        m_registry.emplace<TransformComponent>(entity);
        m_registry.emplace<HierarchyComponent>(entity);
        return entity;
    }

    bool SceneWorld::DestroyEntity(entt::entity entity)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }

        if (auto const *hierarchy = m_registry.try_get<HierarchyComponent>(entity))
        {
            if (const entt::entity parent = hierarchy->parent; parent != entt::null)
            {
                RemoveChild(parent, entity);
            }
            for (entt::entity child : hierarchy->children)
            {
                if (auto *child_hierarchy = m_registry.try_get<HierarchyComponent>(child))
                {
                    child_hierarchy->parent = entt::null;
                }
            }
        }

        m_registry.destroy(entity);
        return true;
    }

    bool SceneWorld::SetName(const entt::entity entity, const std::string &name)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }
        m_registry.emplace_or_replace<NameComponent>(entity, NameComponent{name});
        return true;
    }

    bool SceneWorld::SetLocalTransform(const entt::entity entity, const Transform &local)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }

        auto &transform = m_registry.get_or_emplace<TransformComponent>(entity);
        transform.local = local;
        transform.dirty = true;
        EmitComponentUpdated<TransformComponent>(m_registry, entity);
        MarkDirty(entity);
        return true;
    }

    bool SceneWorld::SetLocalTranslation(const entt::entity entity, const glm::vec3 &translation)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }

        auto &transform = m_registry.get_or_emplace<TransformComponent>(entity);
        transform.local.translation = translation;
        transform.dirty = true;
        EmitComponentUpdated<TransformComponent>(m_registry, entity);
        MarkDirty(entity);
        return true;
    }

    bool SceneWorld::SetLocalRotation(const entt::entity entity, const glm::quat &rotation)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }

        auto &transform = m_registry.get_or_emplace<TransformComponent>(entity);
        transform.local.rotation = rotation;
        transform.dirty = true;
        EmitComponentUpdated<TransformComponent>(m_registry, entity);
        MarkDirty(entity);
        return true;
    }

    bool SceneWorld::SetLocalScale(const entt::entity entity, const glm::vec3 &scale)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }

        auto &transform = m_registry.get_or_emplace<TransformComponent>(entity);
        transform.local.scale = scale;
        transform.dirty = true;
        EmitComponentUpdated<TransformComponent>(m_registry, entity);
        MarkDirty(entity);
        return true;
    }

    bool SceneWorld::IsValid(entt::entity entity) const
    {
        return m_registry.valid(entity);
    }

    const NameComponent *SceneWorld::TryGetName(const entt::entity entity) const
    {
        return m_registry.try_get<NameComponent>(entity);
    }

    const TransformComponent *SceneWorld::TryGetTransform(const entt::entity entity) const
    {
        return m_registry.try_get<TransformComponent>(entity);
    }

    const HierarchyComponent *SceneWorld::TryGetHierarchy(const entt::entity entity) const
    {
        return m_registry.try_get<HierarchyComponent>(entity);
    }

    const MeshRendererComponent *SceneWorld::TryGetMeshRenderer(const entt::entity entity) const
    {
        return m_registry.try_get<MeshRendererComponent>(entity);
    }

    const CameraComponent *SceneWorld::TryGetCamera(const entt::entity entity) const
    {
        return m_registry.try_get<CameraComponent>(entity);
    }

    const PrimaryCameraComponent *SceneWorld::TryGetPrimaryCamera(const entt::entity entity) const
    {
        static const PrimaryCameraComponent k_primary_camera_tag{};
        return m_registry.all_of<PrimaryCameraComponent>(entity) ? &k_primary_camera_tag : nullptr;
    }

    const CameraTargetComponent *SceneWorld::TryGetCameraTarget(const entt::entity entity) const
    {
        return m_registry.try_get<CameraTargetComponent>(entity);
    }

    const LightCommonComponent *SceneWorld::TryGetLightCommon(const entt::entity entity) const
    {
        return m_registry.try_get<LightCommonComponent>(entity);
    }

    const DirectionalLightComponent *SceneWorld::TryGetDirectionalLight(const entt::entity entity) const
    {
        static const DirectionalLightComponent k_directional_light_tag{};
        return m_registry.all_of<DirectionalLightComponent>(entity) ? &k_directional_light_tag : nullptr;
    }

    const PointLightComponent *SceneWorld::TryGetPointLight(const entt::entity entity) const
    {
        return m_registry.try_get<PointLightComponent>(entity);
    }

    const AreaLightComponent *SceneWorld::TryGetAreaLight(const entt::entity entity) const
    {
        return m_registry.try_get<AreaLightComponent>(entity);
    }

    const HdriLightComponent *SceneWorld::TryGetHdriLight(const entt::entity entity) const
    {
        return m_registry.try_get<HdriLightComponent>(entity);
    }

    bool SceneWorld::SetParent(entt::entity child, entt::entity parent)
    {
        if (child == entt::null || child == parent)
        {
            return false;
        }
        if (!m_registry.valid(child))
        {
            return false;
        }

        if (parent != entt::null && !m_registry.valid(parent))
        {
            return false;
        }

        if (parent != entt::null && IsAncestor(child, parent))
        {
            return false;
        }

        auto &child_hierarchy = m_registry.get_or_emplace<HierarchyComponent>(child);
        const entt::entity previous_parent = child_hierarchy.parent;

        if (previous_parent == parent)
        {
            return true;
        }

        if (previous_parent != entt::null)
        {
            RemoveChild(previous_parent, child);
        }

        child_hierarchy.parent = parent;
        EmitComponentUpdated<HierarchyComponent>(m_registry, child);
        EnqueueDirty(DirtyQueueKind::Hierarchy, child);

        if (parent != entt::null)
        {
            auto &parent_hierarchy = m_registry.get_or_emplace<HierarchyComponent>(parent);
            parent_hierarchy.children.push_back(child);
            EmitComponentUpdated<HierarchyComponent>(m_registry, parent);
            EnqueueDirty(DirtyQueueKind::Hierarchy, parent);
        }

        if (previous_parent != entt::null && m_registry.valid(previous_parent))
        {
            EmitComponentUpdated<HierarchyComponent>(m_registry, previous_parent);
            EnqueueDirty(DirtyQueueKind::Hierarchy, previous_parent);
        }

        MarkStructureChanged();
        MarkDirtyRecursive(child);
        return true;
    }

    bool SceneWorld::ClearParent(const entt::entity child)
    {
        return SetParent(child, entt::null);
    }

    bool SceneWorld::AddCamera(const entt::entity entity, const CameraComponent &camera)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }
        m_registry.emplace_or_replace<CameraComponent>(entity, camera);
        m_registry.emplace_or_replace<CameraTargetComponent>(entity);
        return true;
    }

    bool SceneWorld::RemoveCamera(const entt::entity entity)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }
        m_registry.remove<CameraComponent>(entity);
        m_registry.remove<PrimaryCameraComponent>(entity);
        m_registry.remove<CameraTargetComponent>(entity);
        return true;
    }

    bool SceneWorld::SetCameraLens(const entt::entity entity,
                                   const float horizontal_fov_radians,
                                   const float near_plane,
                                   const float far_plane)
    {
        if (!m_registry.valid(entity) || !m_registry.all_of<CameraComponent>(entity))
        {
            return false;
        }

        auto &camera = m_registry.get<CameraComponent>(entity);
        camera.horizontal_fov_radians = std::max(0.0174533f, horizontal_fov_radians);
        camera.near_plane = std::max(0.001f, near_plane);
        camera.far_plane = std::max(camera.near_plane + 0.001f, far_plane);
        EmitComponentUpdated<CameraComponent>(m_registry, entity);
        return true;
    }

    bool SceneWorld::SetPrimaryCamera(const entt::entity entity, const bool is_primary)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }

        if (!is_primary)
        {
            m_registry.remove<PrimaryCameraComponent>(entity);
            return true;
        }

        if (!m_registry.all_of<CameraComponent>(entity))
        {
            return false;
        }

        auto view = m_registry.view<PrimaryCameraComponent>();
        for (const entt::entity existing_primary : view)
        {
            if (existing_primary != entity)
            {
                m_registry.remove<PrimaryCameraComponent>(existing_primary);
            }
        }

        m_registry.emplace_or_replace<PrimaryCameraComponent>(entity);
        return true;
    }

    bool SceneWorld::SetCameraTarget(const entt::entity entity, const bool enabled, entt::entity target)
    {
        if (!m_registry.valid(entity) ||
            !m_registry.all_of<CameraComponent>(entity) ||
            !m_registry.all_of<CameraTargetComponent>(entity))
        {
            return false;
        }

        if (target != entt::null && !m_registry.valid(target))
        {
            target = entt::null;
        }

        auto &camera_target = m_registry.get<CameraTargetComponent>(entity);
        camera_target.enabled = enabled;
        camera_target.target = target;
        EmitComponentUpdated<CameraTargetComponent>(m_registry, entity);
        return true;
    }

    bool SceneWorld::AddMeshRenderer(const entt::entity entity, const MeshRendererComponent &mesh_renderer)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }
        m_registry.emplace_or_replace<MeshRendererComponent>(entity, mesh_renderer);
        return true;
    }

    bool SceneWorld::RemoveMeshRenderer(const entt::entity entity)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }
        m_registry.remove<MeshRendererComponent>(entity);
        return true;
    }

    bool SceneWorld::SetMeshRenderer(const entt::entity entity, const MeshRendererComponent &mesh_renderer)
    {
        return AddMeshRenderer(entity, mesh_renderer);
    }

    bool SceneWorld::AddDirectionalLight(const entt::entity entity,
                                         const LightCommonComponent &common,
                                         const DirectionalLightComponent &)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }
        m_registry.emplace_or_replace<LightCommonComponent>(entity, common);
        m_registry.emplace_or_replace<DirectionalLightComponent>(entity);
        return true;
    }

    bool SceneWorld::AddPointLight(const entt::entity entity,
                                   const LightCommonComponent &common,
                                   const PointLightComponent &point)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }
        m_registry.emplace_or_replace<LightCommonComponent>(entity, common);
        m_registry.emplace_or_replace<PointLightComponent>(entity, point);
        return true;
    }

    bool SceneWorld::AddAreaLight(const entt::entity entity,
                                  const LightCommonComponent &common,
                                  const AreaLightComponent &area)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }
        m_registry.emplace_or_replace<LightCommonComponent>(entity, common);
        m_registry.emplace_or_replace<AreaLightComponent>(entity, area);
        return true;
    }

    bool SceneWorld::AddHdriLight(const entt::entity entity,
                                  const LightCommonComponent &common,
                                  const HdriLightComponent &hdri)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }
        m_registry.emplace_or_replace<LightCommonComponent>(entity, common);
        m_registry.emplace_or_replace<HdriLightComponent>(entity, hdri);
        return true;
    }

    bool SceneWorld::RemoveLight(const entt::entity entity)
    {
        if (!m_registry.valid(entity))
        {
            return false;
        }

        m_registry.remove<LightCommonComponent>(entity);
        m_registry.remove<DirectionalLightComponent>(entity);
        m_registry.remove<PointLightComponent>(entity);
        m_registry.remove<AreaLightComponent>(entity);
        m_registry.remove<HdriLightComponent>(entity);
        return true;
    }

    bool SceneWorld::SetLightCommon(const entt::entity entity, const LightCommonComponent &common)
    {
        if (!m_registry.valid(entity) || !m_registry.all_of<LightCommonComponent>(entity))
        {
            return false;
        }
        auto &light = m_registry.get<LightCommonComponent>(entity);
        light = common;
        EmitComponentUpdated<LightCommonComponent>(m_registry, entity);
        return true;
    }

    bool SceneWorld::SetPointLight(const entt::entity entity, const PointLightComponent &point)
    {
        if (!m_registry.valid(entity) || !m_registry.all_of<PointLightComponent>(entity))
        {
            return false;
        }
        auto &light = m_registry.get<PointLightComponent>(entity);
        light = point;
        EmitComponentUpdated<PointLightComponent>(m_registry, entity);
        return true;
    }

    bool SceneWorld::SetAreaLight(const entt::entity entity, const AreaLightComponent &area)
    {
        if (!m_registry.valid(entity) || !m_registry.all_of<AreaLightComponent>(entity))
        {
            return false;
        }
        auto &light = m_registry.get<AreaLightComponent>(entity);
        light = area;
        EmitComponentUpdated<AreaLightComponent>(m_registry, entity);
        return true;
    }

    bool SceneWorld::SetHdriLight(const entt::entity entity, const HdriLightComponent &hdri)
    {
        if (!m_registry.valid(entity) || !m_registry.all_of<HdriLightComponent>(entity))
        {
            return false;
        }
        auto &light = m_registry.get<HdriLightComponent>(entity);
        light = hdri;
        EmitComponentUpdated<HdriLightComponent>(m_registry, entity);
        return true;
    }

    entt::entity SceneWorld::GetParent(entt::entity child) const
    {
        const auto *hierarchy = m_registry.try_get<HierarchyComponent>(child);
        if (!hierarchy)
        {
            return entt::null;
        }
        return hierarchy->parent;
    }

    const std::vector<entt::entity> &SceneWorld::GetChildren(entt::entity parent) const
    {
        const auto *hierarchy = m_registry.try_get<HierarchyComponent>(parent);
        if (!hierarchy)
        {
            return m_empty_children;
        }
        return hierarchy->children;
    }

    uint32_t SceneWorld::GetEntityCount() const
    {
        uint32_t count = 0;
        const auto entities = m_registry.view<entt::entity>();
        for (const entt::entity entity : entities)
        {
            static_cast<void>(entity);
            ++count;
        }
        return count;
    }

    void SceneWorld::GetEntities(std::vector<entt::entity> &out_entities) const
    {
        out_entities.clear();
        const auto entities = m_registry.view<entt::entity>();
        for (const entt::entity entity : entities)
        {
            out_entities.push_back(entity);
        }
    }

    void SceneWorld::GetEntitiesWithTransform(std::vector<entt::entity> &out_entities) const
    {
        out_entities.clear();
        const auto view = m_registry.view<const TransformComponent>();
        for (const entt::entity entity : view)
        {
            out_entities.push_back(entity);
        }
    }

    void SceneWorld::GetEntitiesWithMeshRenderer(std::vector<entt::entity> &out_entities) const
    {
        out_entities.clear();
        const auto view = m_registry.view<const MeshRendererComponent>();
        for (const entt::entity entity : view)
        {
            out_entities.push_back(entity);
        }
    }

    void SceneWorld::GetEntitiesWithCamera(std::vector<entt::entity> &out_entities) const
    {
        out_entities.clear();
        const auto view = m_registry.view<const CameraComponent>();
        for (const entt::entity entity : view)
        {
            out_entities.push_back(entity);
        }
    }

    void SceneWorld::GetEntitiesWithLight(std::vector<entt::entity> &out_entities) const
    {
        out_entities.clear();
        const auto view = m_registry.view<const LightCommonComponent>();
        for (const entt::entity entity : view)
        {
            out_entities.push_back(entity);
        }
    }

    void SceneWorld::MarkDirty(entt::entity entity)
    {
        MarkDirtyRecursive(entity);
    }

    void SceneWorld::FlushPendingChanges()
    {
        UpdateTransforms();
    }

    void SceneWorld::UpdateTransforms()
    {
        auto view = m_registry.view<TransformComponent>();
        for (entt::entity entity : view)
        {
            const auto *hierarchy = m_registry.try_get<HierarchyComponent>(entity);
            if (const bool is_root = (!hierarchy || hierarchy->parent == entt::null); !is_root)
            {
                continue;
            }

            UpdateNodeRecursive(entity, glm::mat4(1.0f), false);
        }
    }

    void SceneWorld::RemoveChild(entt::entity parent, entt::entity child)
    {
        if (parent == entt::null)
        {
            return;
        }
        auto *hierarchy = m_registry.try_get<HierarchyComponent>(parent);
        if (!hierarchy)
        {
            return;
        }
        auto &children = hierarchy->children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }

    void SceneWorld::MarkDirtyRecursive(entt::entity entity)
    {
        if (auto *transform = m_registry.try_get<TransformComponent>(entity))
        {
            transform->dirty = true;
            EnqueueDirty(DirtyQueueKind::Transform, entity);
        }

        const auto *hierarchy = m_registry.try_get<HierarchyComponent>(entity);
        if (!hierarchy)
        {
            return;
        }

        for (entt::entity child : hierarchy->children)
        {
            MarkDirtyRecursive(child);
        }
    }

    void SceneWorld::UpdateNodeRecursive(entt::entity entity, const glm::mat4 &parent_world, bool parent_dirty)
    {
        glm::mat4 world = parent_world;
        bool dirty = parent_dirty;

        if (auto *transform = m_registry.try_get<TransformComponent>(entity))
        {
            dirty = dirty || transform->dirty;
            if (dirty)
            {
                world = parent_world * ComposeMatrix(transform->local);
                transform->world = world;
                transform->dirty = false;
            }
            else
            {
                world = transform->world;
            }
        }

        const auto *hierarchy = m_registry.try_get<HierarchyComponent>(entity);
        if (!hierarchy)
        {
            return;
        }

        for (entt::entity child : hierarchy->children)
        {
            UpdateNodeRecursive(child, world, dirty);
        }
    }

    bool SceneWorld::IsAncestor(entt::entity ancestor, entt::entity entity) const
    {
        entt::entity current = entity;
        while (current != entt::null)
        {
            if (current == ancestor)
            {
                return true;
            }
            const auto *hierarchy = m_registry.try_get<HierarchyComponent>(current);
            if (!hierarchy)
            {
                break;
            }
            current = hierarchy->parent;
        }
        return false;
    }

    void SceneWorld::EnqueueDirty(const DirtyQueueKind kind, const entt::entity entity)
    {
        const uint32_t key = EntityKey(entity);
        switch (kind)
        {
        case DirtyQueueKind::Transform:
            if (m_transform_dedup.insert(key).second)
            {
                m_render_dirty_queues.transform_entities.push_back(entity);
            }
            break;
        case DirtyQueueKind::Mesh:
            if (m_mesh_dedup.insert(key).second)
            {
                m_render_dirty_queues.mesh_entities.push_back(entity);
            }
            break;
        case DirtyQueueKind::Light:
            if (m_light_dedup.insert(key).second)
            {
                m_render_dirty_queues.light_entities.push_back(entity);
            }
            break;
        case DirtyQueueKind::Hierarchy:
            if (m_hierarchy_dedup.insert(key).second)
            {
                m_render_dirty_queues.hierarchy_entities.push_back(entity);
            }
            break;
        case DirtyQueueKind::Destroyed:
            if (m_destroyed_dedup.insert(key).second)
            {
                m_render_dirty_queues.destroyed_entities.push_back(entity);
            }
            break;
        }
    }

    void SceneWorld::MarkStructureChanged()
    {
        m_render_dirty_queues.structure_changed = true;
    }

    void SceneWorld::ResetDirtyDedup()
    {
        m_transform_dedup.clear();
        m_mesh_dedup.clear();
        m_light_dedup.clear();
        m_hierarchy_dedup.clear();
        m_destroyed_dedup.clear();
    }

    void SceneWorld::OnConstructTransform(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Transform, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnUpdateTransform(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Transform, entity);
    }

    void SceneWorld::OnDestroyTransform(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Destroyed, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnConstructHierarchy(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Hierarchy, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnUpdateHierarchy(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Hierarchy, entity);
    }

    void SceneWorld::OnDestroyHierarchy(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Destroyed, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnConstructMeshRenderer(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Mesh, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnUpdateMeshRenderer(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Mesh, entity);
    }

    void SceneWorld::OnDestroyMeshRenderer(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Destroyed, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnConstructLightCommon(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Light, entity);
    }

    void SceneWorld::OnUpdateLightCommon(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Light, entity);
    }

    void SceneWorld::OnDestroyLightCommon(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Destroyed, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnConstructDirectionalLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Light, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnUpdateDirectionalLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Light, entity);
    }

    void SceneWorld::OnDestroyDirectionalLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Destroyed, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnConstructPointLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Light, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnUpdatePointLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Light, entity);
    }

    void SceneWorld::OnDestroyPointLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Destroyed, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnConstructAreaLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Light, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnUpdateAreaLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Light, entity);
    }

    void SceneWorld::OnDestroyAreaLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Destroyed, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnConstructHdriLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Light, entity);
        MarkStructureChanged();
    }

    void SceneWorld::OnUpdateHdriLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Light, entity);
    }

    void SceneWorld::OnDestroyHdriLight(entt::registry &, entt::entity entity)
    {
        EnqueueDirty(DirtyQueueKind::Destroyed, entity);
        MarkStructureChanged();
    }

} // namespace hybrid::core::scene
