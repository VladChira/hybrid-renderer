#include "core/scene/SceneWorld.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace hybrid::core::scene
{

    namespace
    {
        glm::mat4 ComposeMatrix(const Transform &transform)
        {
            glm::mat4 result(1.0f);
            result = glm::translate(result, transform.translation);
            result *= glm::mat4_cast(transform.rotation);
            result = glm::scale(result, transform.scale);
            return result;
        }
    } // namespace

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

        if (auto *hierarchy = m_registry.try_get<HierarchyComponent>(entity))
        {
            const entt::entity parent = hierarchy->parent;
            if (parent != entt::null)
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

} // namespace hybrid::core::scene
