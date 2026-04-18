#include "renderer/SceneWorldSnapshot.h"

#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

        uint32_t EntityKey(const entt::entity entity)
        {
            return static_cast<uint32_t>(entt::to_integral(entity));
        }

        std::vector<RenderMeshInstance> &ResolveBucketVector(FrameSceneData &frame_data, const InstanceBucket bucket)
        {
            switch (bucket)
            {
            case InstanceBucket::Opaque:
                return frame_data.opaque_mesh_instances;
            case InstanceBucket::Masked:
                return frame_data.masked_mesh_instances;
            case InstanceBucket::Blended:
                return frame_data.blended_mesh_instances;
            }

            return frame_data.opaque_mesh_instances;
        }
    } // namespace

    struct SceneFrameCache::Impl
    {
        struct MeshRecord
        {
            InstanceBucket bucket = InstanceBucket::Opaque;
            size_t index = 0;
        };

        FrameSceneData frame_data{};
        bool initialized = false;
        bool mesh_bounds_dirty = false;

        std::unordered_map<uint32_t, MeshRecord> mesh_records{};
        std::unordered_map<uint32_t, size_t> directional_light_records{};
        std::unordered_map<uint32_t, size_t> point_light_records{};
        std::unordered_map<uint32_t, size_t> area_light_records{};
        std::unordered_map<uint32_t, size_t> hdri_light_records{};
    };

    SceneFrameCache::SceneFrameCache() = default;
    SceneFrameCache::~SceneFrameCache() = default;
    SceneFrameCache::SceneFrameCache(SceneFrameCache &&) noexcept = default;
    SceneFrameCache &SceneFrameCache::operator=(SceneFrameCache &&) noexcept = default;

    namespace
    {
        template <typename TLight>
        bool RemoveLight(std::unordered_map<uint32_t, size_t> &index_map,
                         std::vector<TLight> &lights,
                         const uint32_t key)
        {
            const auto it = index_map.find(key);
            if (it == index_map.end())
            {
                return false;
            }

            const size_t index = it->second;
            const size_t last = lights.size() - 1;
            if (index != last)
            {
                lights[index] = lights[last];
                const uint32_t moved_key = static_cast<uint32_t>(lights[index].instance_id);
                index_map[moved_key] = index;
            }

            lights.pop_back();
            index_map.erase(it);
            return true;
        }

        bool RemoveMesh(SceneFrameCache::Impl &impl, const uint32_t key)
        {
            const auto it = impl.mesh_records.find(key);
            if (it == impl.mesh_records.end())
            {
                return false;
            }

            const SceneFrameCache::Impl::MeshRecord record = it->second;
            std::vector<RenderMeshInstance> &bucket = ResolveBucketVector(impl.frame_data, record.bucket);
            const size_t last = bucket.size() - 1;
            if (record.index != last)
            {
                bucket[record.index] = bucket[last];
                const uint32_t moved_key = static_cast<uint32_t>(bucket[record.index].instance_id);
                impl.mesh_records[moved_key] = SceneFrameCache::Impl::MeshRecord{record.bucket, record.index};
            }

            bucket.pop_back();
            impl.mesh_records.erase(it);
            impl.mesh_bounds_dirty = true;
            return true;
        }

        bool UpsertMesh(SceneFrameCache::Impl &impl, const entt::registry &registry, const entt::entity entity)
        {
            if (!registry.valid(entity) ||
                !registry.all_of<core::scene::TransformComponent, core::scene::MeshRendererComponent>(entity))
            {
                return RemoveMesh(impl, EntityKey(entity));
            }

            const auto &[transform, mesh_renderer] =
                registry.get<const core::scene::TransformComponent, const core::scene::MeshRendererComponent>(entity);
            if (!mesh_renderer.mesh.IsValid())
            {
                return RemoveMesh(impl, EntityKey(entity));
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

            const InstanceBucket new_bucket = ResolveInstanceBucket(mesh);
            const uint32_t key = EntityKey(entity);
            const auto record_it = impl.mesh_records.find(key);
            if (record_it == impl.mesh_records.end())
            {
                std::vector<RenderMeshInstance> &bucket = ResolveBucketVector(impl.frame_data, new_bucket);
                const size_t index = bucket.size();
                bucket.push_back(instance);
                impl.mesh_records.emplace(key, SceneFrameCache::Impl::MeshRecord{new_bucket, index});
                impl.mesh_bounds_dirty = true;
                return true;
            }

            const SceneFrameCache::Impl::MeshRecord old_record = record_it->second;
            if (old_record.bucket == new_bucket)
            {
                std::vector<RenderMeshInstance> &bucket = ResolveBucketVector(impl.frame_data, new_bucket);
                bucket[old_record.index] = instance;
                impl.mesh_bounds_dirty = true;
                return true;
            }

            RemoveMesh(impl, key);
            std::vector<RenderMeshInstance> &bucket = ResolveBucketVector(impl.frame_data, new_bucket);
            const size_t index = bucket.size();
            bucket.push_back(instance);
            impl.mesh_records.emplace(key, SceneFrameCache::Impl::MeshRecord{new_bucket, index});
            impl.mesh_bounds_dirty = true;
            return true;
        }

        bool UpsertDirectionalLight(SceneFrameCache::Impl &impl, const entt::registry &registry, const entt::entity entity)
        {
            const uint32_t key = EntityKey(entity);
            if (!registry.valid(entity) ||
                !registry.all_of<core::scene::TransformComponent,
                                 core::scene::LightCommonComponent,
                                 core::scene::DirectionalLightComponent>(entity))
            {
                return RemoveLight(impl.directional_light_records, impl.frame_data.directional_lights, key);
            }

            const auto &transform = registry.get<const core::scene::TransformComponent>(entity);
            const auto &common = registry.get<const core::scene::LightCommonComponent>(entity);
            RenderDirectionalLight light{};
            light.instance_id = static_cast<uint64_t>(entt::to_integral(entity));
            light.direction = TransformDirection(transform.world, kDirectionalLightLocalAxis);
            light.color = common.color;
            light.intensity = common.intensity;
            light.cast_shadows = common.cast_shadows;

            const auto it = impl.directional_light_records.find(key);
            if (it == impl.directional_light_records.end())
            {
                const size_t index = impl.frame_data.directional_lights.size();
                impl.frame_data.directional_lights.push_back(light);
                impl.directional_light_records.emplace(key, index);
            }
            else
            {
                impl.frame_data.directional_lights[it->second] = light;
            }
            return true;
        }

        bool UpsertPointLight(SceneFrameCache::Impl &impl, const entt::registry &registry, const entt::entity entity)
        {
            const uint32_t key = EntityKey(entity);
            if (!registry.valid(entity) ||
                !registry.all_of<core::scene::TransformComponent,
                                 core::scene::LightCommonComponent,
                                 core::scene::PointLightComponent>(entity))
            {
                return RemoveLight(impl.point_light_records, impl.frame_data.point_lights, key);
            }

            const auto &[transform, common, point] =
                registry.get<const core::scene::TransformComponent,
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

            const auto it = impl.point_light_records.find(key);
            if (it == impl.point_light_records.end())
            {
                const size_t index = impl.frame_data.point_lights.size();
                impl.frame_data.point_lights.push_back(light);
                impl.point_light_records.emplace(key, index);
            }
            else
            {
                impl.frame_data.point_lights[it->second] = light;
            }
            return true;
        }

        bool UpsertAreaLight(SceneFrameCache::Impl &impl, const entt::registry &registry, const entt::entity entity)
        {
            const uint32_t key = EntityKey(entity);
            if (!registry.valid(entity) ||
                !registry.all_of<core::scene::TransformComponent,
                                 core::scene::LightCommonComponent,
                                 core::scene::AreaLightComponent>(entity))
            {
                return RemoveLight(impl.area_light_records, impl.frame_data.area_lights, key);
            }

            const auto &[transform, common, area] =
                registry.get<const core::scene::TransformComponent,
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
            light.visible = area.visible;

            const auto it = impl.area_light_records.find(key);
            if (it == impl.area_light_records.end())
            {
                const size_t index = impl.frame_data.area_lights.size();
                impl.frame_data.area_lights.push_back(light);
                impl.area_light_records.emplace(key, index);
            }
            else
            {
                impl.frame_data.area_lights[it->second] = light;
            }
            return true;
        }

        bool UpsertHdriLight(SceneFrameCache::Impl &impl, const entt::registry &registry, const entt::entity entity)
        {
            const uint32_t key = EntityKey(entity);
            if (!registry.valid(entity) ||
                !registry.all_of<core::scene::LightCommonComponent,
                                 core::scene::HdriLightComponent>(entity))
            {
                return RemoveLight(impl.hdri_light_records, impl.frame_data.hdri_lights, key);
            }

            const auto &[common, hdri] =
                registry.get<const core::scene::LightCommonComponent,
                             const core::scene::HdriLightComponent>(entity);
            RenderHdriLight light{};
            light.instance_id = static_cast<uint64_t>(entt::to_integral(entity));
            light.intensity = common.intensity;
            light.yaw_radians = hdri.yaw_radians;
            light.cast_shadows = common.cast_shadows;
            light.texture = hdri.texture;

            const auto it = impl.hdri_light_records.find(key);
            if (it == impl.hdri_light_records.end())
            {
                const size_t index = impl.frame_data.hdri_lights.size();
                impl.frame_data.hdri_lights.push_back(light);
                impl.hdri_light_records.emplace(key, index);
            }
            else
            {
                impl.frame_data.hdri_lights[it->second] = light;
            }
            return true;
        }

        void RecomputeSceneBounds(SceneFrameCache::Impl &impl)
        {
            impl.frame_data.scene_bounds = {};
            for (const RenderMeshInstance &instance : impl.frame_data.opaque_mesh_instances)
            {
                MergeBounds(impl.frame_data.scene_bounds, instance.world_bounds);
            }
            for (const RenderMeshInstance &instance : impl.frame_data.masked_mesh_instances)
            {
                MergeBounds(impl.frame_data.scene_bounds, instance.world_bounds);
            }
            for (const RenderMeshInstance &instance : impl.frame_data.blended_mesh_instances)
            {
                MergeBounds(impl.frame_data.scene_bounds, instance.world_bounds);
            }
            impl.mesh_bounds_dirty = false;
        }

        void RebuildIndices(SceneFrameCache::Impl &impl)
        {
            impl.mesh_records.clear();
            impl.directional_light_records.clear();
            impl.point_light_records.clear();
            impl.area_light_records.clear();
            impl.hdri_light_records.clear();

            for (size_t i = 0; i < impl.frame_data.opaque_mesh_instances.size(); ++i)
            {
                const uint32_t key = static_cast<uint32_t>(impl.frame_data.opaque_mesh_instances[i].instance_id);
                impl.mesh_records[key] = SceneFrameCache::Impl::MeshRecord{InstanceBucket::Opaque, i};
            }
            for (size_t i = 0; i < impl.frame_data.masked_mesh_instances.size(); ++i)
            {
                const uint32_t key = static_cast<uint32_t>(impl.frame_data.masked_mesh_instances[i].instance_id);
                impl.mesh_records[key] = SceneFrameCache::Impl::MeshRecord{InstanceBucket::Masked, i};
            }
            for (size_t i = 0; i < impl.frame_data.blended_mesh_instances.size(); ++i)
            {
                const uint32_t key = static_cast<uint32_t>(impl.frame_data.blended_mesh_instances[i].instance_id);
                impl.mesh_records[key] = SceneFrameCache::Impl::MeshRecord{InstanceBucket::Blended, i};
            }
            for (size_t i = 0; i < impl.frame_data.directional_lights.size(); ++i)
            {
                impl.directional_light_records[static_cast<uint32_t>(impl.frame_data.directional_lights[i].instance_id)] = i;
            }
            for (size_t i = 0; i < impl.frame_data.point_lights.size(); ++i)
            {
                impl.point_light_records[static_cast<uint32_t>(impl.frame_data.point_lights[i].instance_id)] = i;
            }
            for (size_t i = 0; i < impl.frame_data.area_lights.size(); ++i)
            {
                impl.area_light_records[static_cast<uint32_t>(impl.frame_data.area_lights[i].instance_id)] = i;
            }
            for (size_t i = 0; i < impl.frame_data.hdri_lights.size(); ++i)
            {
                impl.hdri_light_records[static_cast<uint32_t>(impl.frame_data.hdri_lights[i].instance_id)] = i;
            }
            impl.mesh_bounds_dirty = false;
        }
    } // namespace

    void SceneFrameCache::Reset()
    {
        if (!m_impl)
        {
            m_impl = std::make_unique<Impl>();
            return;
        }

        *m_impl = {};
    }

    const FrameSceneData &SceneFrameCache::GetFrameData() const
    {
        static FrameSceneData empty{};
        return m_impl ? m_impl->frame_data : empty;
    }

    void SceneFrameCache::Sync(core::scene::SceneWorld &scene_world)
    {
        if (!m_impl)
        {
            m_impl = std::make_unique<Impl>();
        }

        core::scene::SceneWorld::RenderDirtyQueues dirty = scene_world.ConsumeRenderDirtyQueues();
        if (!m_impl->initialized || dirty.structure_changed)
        {
            m_impl->frame_data = BuildFrameSceneData(scene_world);
            RebuildIndices(*m_impl);
            m_impl->initialized = true;
            return;
        }

        const entt::registry &registry = scene_world.Registry();

        for (const entt::entity entity : dirty.destroyed_entities)
        {
            const uint32_t key = EntityKey(entity);
            RemoveMesh(*m_impl, key);
            RemoveLight(m_impl->directional_light_records, m_impl->frame_data.directional_lights, key);
            RemoveLight(m_impl->point_light_records, m_impl->frame_data.point_lights, key);
            RemoveLight(m_impl->area_light_records, m_impl->frame_data.area_lights, key);
            RemoveLight(m_impl->hdri_light_records, m_impl->frame_data.hdri_lights, key);
        }

        std::unordered_set<uint32_t> mesh_refresh_keys{};
        mesh_refresh_keys.reserve(dirty.mesh_entities.size() + dirty.transform_entities.size() + dirty.hierarchy_entities.size());
        for (const entt::entity entity : dirty.mesh_entities)
        {
            mesh_refresh_keys.insert(EntityKey(entity));
        }
        for (const entt::entity entity : dirty.transform_entities)
        {
            mesh_refresh_keys.insert(EntityKey(entity));
        }
        for (const entt::entity entity : dirty.hierarchy_entities)
        {
            mesh_refresh_keys.insert(EntityKey(entity));
        }

        bool touched_mesh = false;
        for (const uint32_t key : mesh_refresh_keys)
        {
            touched_mesh = UpsertMesh(*m_impl, registry, static_cast<entt::entity>(key)) || touched_mesh;
        }

        std::unordered_set<uint32_t> light_refresh_keys{};
        light_refresh_keys.reserve(dirty.light_entities.size() + dirty.transform_entities.size() + dirty.hierarchy_entities.size());
        for (const entt::entity entity : dirty.light_entities)
        {
            light_refresh_keys.insert(EntityKey(entity));
        }
        for (const entt::entity entity : dirty.transform_entities)
        {
            light_refresh_keys.insert(EntityKey(entity));
        }
        for (const entt::entity entity : dirty.hierarchy_entities)
        {
            light_refresh_keys.insert(EntityKey(entity));
        }

        for (const uint32_t key : light_refresh_keys)
        {
            const entt::entity entity = static_cast<entt::entity>(key);
            UpsertDirectionalLight(*m_impl, registry, entity);
            UpsertPointLight(*m_impl, registry, entity);
            UpsertAreaLight(*m_impl, registry, entity);
            UpsertHdriLight(*m_impl, registry, entity);
        }

        if (m_impl->mesh_bounds_dirty || touched_mesh)
        {
            RecomputeSceneBounds(*m_impl);
        }
    }

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
