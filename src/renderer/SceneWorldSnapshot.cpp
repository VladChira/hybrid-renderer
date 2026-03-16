#include "renderer/SceneWorldSnapshot.h"

#include <limits>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace hybrid::renderer
{

    namespace
    {
        core::scene::Aabb TransformAabb(const core::scene::Aabb &bounds, const glm::mat4 &world_from_local)
        {
            if (!bounds.valid)
            {
                return {};
            }

            const glm::vec3 min = bounds.min;
            const glm::vec3 max = bounds.max;
            const glm::vec3 corners[8] = {
                {min.x, min.y, min.z},
                {max.x, min.y, min.z},
                {min.x, max.y, min.z},
                {max.x, max.y, min.z},
                {min.x, min.y, max.z},
                {max.x, min.y, max.z},
                {min.x, max.y, max.z},
                {max.x, max.y, max.z},
            };

            glm::vec3 transformed_min(std::numeric_limits<float>::max());
            glm::vec3 transformed_max(std::numeric_limits<float>::lowest());

            for (const glm::vec3 &corner : corners)
            {
                const glm::vec3 world = glm::vec3(world_from_local * glm::vec4(corner, 1.0f));
                transformed_min = glm::min(transformed_min, world);
                transformed_max = glm::max(transformed_max, world);
            }

            core::scene::Aabb world_bounds{};
            world_bounds.min = transformed_min;
            world_bounds.max = transformed_max;
            world_bounds.valid = true;
            return world_bounds;
        }
    } // namespace

    RenderSceneSnapshot BuildRenderSceneSnapshot(const core::scene::SceneWorld &scene_world)
    {
        RenderSceneSnapshot snapshot{};

        const auto &registry = scene_world.Registry();
        auto view = registry.view<const core::scene::TransformComponent, const core::scene::MeshRendererComponent>();

        snapshot.mesh_instances.reserve(view.size_hint());

        for (const entt::entity entity : view)
        {
            const auto &[transform, mesh_renderer] =
                view.get<const core::scene::TransformComponent, const core::scene::MeshRendererComponent>(entity);

            if (!mesh_renderer.mesh.IsValid())
            {
                continue;
            }

            RenderMeshInstance instance{};
            instance.instance_id = static_cast<uint64_t>(entt::to_integral(entity));
            instance.mesh = mesh_renderer.mesh;
            instance.world_from_local = transform.world;

            if (const core::scene::MeshAsset *mesh = mesh_renderer.mesh.Get())
            {
                instance.world_bounds = TransformAabb(mesh->bounds, transform.world);
            }

            snapshot.mesh_instances.push_back(instance);
        }

        return snapshot;
    }

} // namespace hybrid::renderer
