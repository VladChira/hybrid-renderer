#include "renderer/SceneWorldSnapshot.h"

#include <limits>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace hybrid::renderer
{

    namespace
    {
        // The directional light's direction is derived from its local transform.
        const glm::vec3 kDirectionalLightLocalAxis = glm::vec3(0.0f, -1.0f, 0.0f);

        enum class InstanceBucket
        {
            Opaque,
            Masked,
            Blended
        };

        glm::vec3 TransformPoint(const glm::mat4 &world_from_local, const glm::vec3 &point)
        {
            return glm::vec3(world_from_local * glm::vec4(point, 1.0f));
        }

        glm::vec3 TransformDirection(const glm::mat4 &world_from_local, const glm::vec3 &direction)
        {
            glm::vec3 transformed = glm::mat3(world_from_local) * direction;
            if (glm::dot(transformed, transformed) < 1e-8f)
            {
                return glm::vec3(0.0f, -1.0f, 0.0f);
            }
            return glm::normalize(transformed);
        }

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

        auto directional_view = registry.view<const core::scene::TransformComponent,
                                              const core::scene::LightCommonComponent,
                                              const core::scene::DirectionalLightComponent>();
        frame_data.directional_lights.reserve(directional_view.size_hint());
        for (const entt::entity entity : directional_view)
        {
            const auto &transform = directional_view.get<const core::scene::TransformComponent>(entity);
            const auto &common = directional_view.get<const core::scene::LightCommonComponent>(entity);
            RenderDirectionalLight light{};
            light.instance_id = static_cast<uint64_t>(entt::to_integral(entity));
            light.direction = TransformDirection(transform.world, kDirectionalLightLocalAxis);
            light.color = common.color;
            light.intensity = common.intensity;
            light.cast_shadows = common.cast_shadows;
            frame_data.directional_lights.push_back(light);
        }

        auto point_view = registry.view<const core::scene::TransformComponent,
                                        const core::scene::LightCommonComponent,
                                        const core::scene::PointLightComponent>();
        frame_data.point_lights.reserve(point_view.size_hint());
        for (const entt::entity entity : point_view)
        {
            const auto &[transform, common, point] =
                point_view.get<const core::scene::TransformComponent,
                               const core::scene::LightCommonComponent,
                               const core::scene::PointLightComponent>(entity);
            RenderPointLight light{};
            light.instance_id = static_cast<uint64_t>(entt::to_integral(entity));
            light.position = TransformPoint(transform.world, glm::vec3(0.0f));
            light.color = common.color;
            light.intensity = common.intensity;
            light.range = point.range;
            light.attenuation_constant = point.attenuation_constant;
            light.attenuation_linear = point.attenuation_linear;
            light.attenuation_quadratic = point.attenuation_quadratic;
            light.cast_shadows = common.cast_shadows;
            frame_data.point_lights.push_back(light);
        }

        auto area_view = registry.view<const core::scene::TransformComponent,
                                       const core::scene::LightCommonComponent,
                                       const core::scene::AreaLightComponent>();
        frame_data.area_lights.reserve(area_view.size_hint());
        for (const entt::entity entity : area_view)
        {
            const auto &[transform, common, area] =
                area_view.get<const core::scene::TransformComponent,
                              const core::scene::LightCommonComponent,
                              const core::scene::AreaLightComponent>(entity);
            RenderAreaLight light{};
            light.instance_id = static_cast<uint64_t>(entt::to_integral(entity));
            light.position = TransformPoint(transform.world, glm::vec3(0.0f));
            light.direction = TransformDirection(transform.world, area.direction);
            light.size = area.size;
            light.color = common.color;
            light.intensity = common.intensity;
            light.two_sided = area.two_sided;
            light.cast_shadows = common.cast_shadows;
            frame_data.area_lights.push_back(light);
        }

        auto hdri_view = registry.view<const core::scene::LightCommonComponent,
                                       const core::scene::HdriLightComponent>();
        frame_data.hdri_lights.reserve(hdri_view.size_hint());
        for (const entt::entity entity : hdri_view)
        {
            const auto &[common, hdri] =
                hdri_view.get<const core::scene::LightCommonComponent,
                              const core::scene::HdriLightComponent>(entity);
            RenderHdriLight light{};
            light.instance_id = static_cast<uint64_t>(entt::to_integral(entity));
            light.intensity = common.intensity;
            light.yaw_radians = hdri.yaw_radians;
            light.cast_shadows = common.cast_shadows;
            light.texture = hdri.texture;
            frame_data.hdri_lights.push_back(light);
        }

        return frame_data;
    }

} // namespace hybrid::renderer
