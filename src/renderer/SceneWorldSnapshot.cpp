#include "renderer/SceneWorldSnapshot.h"

#include <limits>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace hybrid::renderer
{

    namespace
    {
        enum class InstanceBucket
        {
            Opaque,
            Masked,
            Blended
        };

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

        void MergeBounds(core::scene::Aabb &accumulated, const core::scene::Aabb &bounds)
        {
            if (!bounds.valid)
            {
                return;
            }

            if (!accumulated.valid)
            {
                accumulated = bounds;
                return;
            }

            accumulated.min = glm::min(accumulated.min, bounds.min);
            accumulated.max = glm::max(accumulated.max, bounds.max);
            accumulated.valid = true;
        }

        InstanceBucket ResolveInstanceBucket(const core::scene::MeshAsset *mesh)
        {
            if (mesh == nullptr)
            {
                return InstanceBucket::Opaque;
            }

            bool has_masked_primitive = false;
            for (const core::scene::MeshPrimitive &primitive : mesh->primitives)
            {
                const core::scene::MaterialAsset *material = primitive.material.Get();
                if (material == nullptr)
                {
                    continue;
                }

                if (material->alpha_mode == core::scene::AlphaMode::Blend)
                {
                    return InstanceBucket::Blended;
                }

                if (material->alpha_mode == core::scene::AlphaMode::Mask)
                {
                    has_masked_primitive = true;
                }
            }

            return has_masked_primitive ? InstanceBucket::Masked : InstanceBucket::Opaque;
        }
    } // namespace

    FrameSceneData BuildFrameSceneData(const core::scene::SceneWorld &scene_world)
    {
        FrameSceneData frame_data{};

        const auto &registry = scene_world.Registry();
        auto view = registry.view<const core::scene::TransformComponent, const core::scene::MeshRendererComponent>();

        const size_t reserve_count = view.size_hint();
        frame_data.opaque_mesh_instances.reserve(reserve_count);
        frame_data.masked_mesh_instances.reserve(reserve_count / 8);
        frame_data.blended_mesh_instances.reserve(reserve_count / 8);

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

            const core::scene::MeshAsset *mesh = mesh_renderer.mesh.Get();
            if (mesh != nullptr)
            {
                instance.world_bounds = TransformAabb(mesh->bounds, transform.world);
            }

            MergeBounds(frame_data.scene_bounds, instance.world_bounds);

            switch (ResolveInstanceBucket(mesh))
            {
            case InstanceBucket::Opaque:
                frame_data.opaque_mesh_instances.push_back(instance);
                break;
            case InstanceBucket::Masked:
                frame_data.masked_mesh_instances.push_back(instance);
                break;
            case InstanceBucket::Blended:
                frame_data.blended_mesh_instances.push_back(instance);
                break;
            }
        }

        return frame_data;
    }

    RenderSceneSnapshot BuildRenderSceneSnapshot(const core::scene::SceneWorld &scene_world)
    {
        FrameSceneData frame_data = BuildFrameSceneData(scene_world);

        RenderSceneSnapshot snapshot{};
        const size_t total_instance_count = frame_data.opaque_mesh_instances.size() +
                                            frame_data.masked_mesh_instances.size() +
                                            frame_data.blended_mesh_instances.size();
        snapshot.mesh_instances.reserve(total_instance_count);

        snapshot.mesh_instances.insert(snapshot.mesh_instances.end(),
                                       frame_data.opaque_mesh_instances.begin(),
                                       frame_data.opaque_mesh_instances.end());
        snapshot.mesh_instances.insert(snapshot.mesh_instances.end(),
                                       frame_data.masked_mesh_instances.begin(),
                                       frame_data.masked_mesh_instances.end());
        snapshot.mesh_instances.insert(snapshot.mesh_instances.end(),
                                       frame_data.blended_mesh_instances.begin(),
                                       frame_data.blended_mesh_instances.end());
        return snapshot;
    }

} // namespace hybrid::renderer
