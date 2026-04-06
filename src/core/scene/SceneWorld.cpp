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

    void SceneWorld::DestroyEntity(entt::entity entity)
    {
        if (!m_registry.valid(entity))
        {
            return;
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
    }

    bool SceneWorld::IsValid(entt::entity entity) const
    {
        return m_registry.valid(entity);
    }

    void SceneWorld::SetParent(entt::entity child, entt::entity parent)
    {
        if (child == entt::null || child == parent)
        {
            return;
        }
        if (!m_registry.valid(child))
        {
            return;
        }

        if (parent != entt::null && !m_registry.valid(parent))
        {
            return;
        }

        if (parent != entt::null && IsAncestor(child, parent))
        {
            return;
        }

        auto &child_hierarchy = m_registry.get_or_emplace<HierarchyComponent>(child);

        if (child_hierarchy.parent != entt::null)
        {
            RemoveChild(child_hierarchy.parent, child);
        }

        child_hierarchy.parent = parent;

        if (parent != entt::null)
        {
            auto &parent_hierarchy = m_registry.get_or_emplace<HierarchyComponent>(parent);
            parent_hierarchy.children.push_back(child);
        }

        MarkDirtyRecursive(child);
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

    void SceneWorld::MarkDirty(entt::entity entity)
    {
        MarkDirtyRecursive(entity);
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
