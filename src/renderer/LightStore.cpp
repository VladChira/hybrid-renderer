#include "renderer/LightStore.h"

#include "renderer/FrameResources.h"
#include "renderer/ShaderBindings.h"

#include <algorithm>

namespace hybrid::renderer
{

    namespace
    {
        constexpr size_t kMinCapacityBytes = 1024;

        size_t GrowCapacityBytes(size_t needed_bytes, size_t current_capacity_bytes)
        {
            if (needed_bytes == 0)
            {
                return current_capacity_bytes;
            }
            size_t target = std::max(current_capacity_bytes * 2, kMinCapacityBytes);
            while (target < needed_bytes)
            {
                target *= 2;
            }
            return target;
        }

        bool UploadBuffer(GLBuffer &buffer,
                          const void *data,
                          size_t needed_bytes,
                          size_t &capacity_bytes)
        {
            if (!buffer.IsValid())
            {
                return false;
            }
            buffer.Bind();
            if (needed_bytes == 0)
            {
                GLBuffer::Unbind(GL_SHADER_STORAGE_BUFFER);
                return true;
            }
            if (needed_bytes > capacity_bytes)
            {
                capacity_bytes = GrowCapacityBytes(needed_bytes, capacity_bytes);
                glBufferData(GL_SHADER_STORAGE_BUFFER,
                             static_cast<GLsizeiptr>(capacity_bytes),
                             nullptr,
                             GL_DYNAMIC_DRAW);
            }
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                            static_cast<GLsizeiptr>(needed_bytes),
                            data);
            GLBuffer::Unbind(GL_SHADER_STORAGE_BUFFER);
            return true;
        }
    } // namespace

    LightStore::LightStore() = default;

    bool LightStore::Init()
    {
        if (m_initialized)
        {
            return true;
        }
        if (!m_directional_buffer.Create(GL_SHADER_STORAGE_BUFFER)) { return false; }
        if (!m_point_buffer.Create(GL_SHADER_STORAGE_BUFFER))       { return false; }
        if (!m_area_buffer.Create(GL_SHADER_STORAGE_BUFFER))        { return false; }
        m_initialized = true;
        return true;
    }

    bool LightStore::Update(const FrameSceneData &scene, bool enable_shadows)
    {
        if (!m_initialized)
        {
            return false;
        }

        m_shadow_casters.clear();
        m_directional.clear();
        m_point.clear();
        m_area.clear();

        auto allocate_shadow_layer = [&]() -> int32_t
        {
            if (!enable_shadows)
            {
                return -1;
            }
            if (m_shadow_casters.size() >= static_cast<size_t>(kMaxShadowMaskLayers))
            {
                return -1;
            }
            return static_cast<int32_t>(m_shadow_casters.size());
        };

        m_directional.reserve(scene.directional_lights.size());
        for (const RenderDirectionalLight &light : scene.directional_lights)
        {
            const int32_t layer = light.cast_shadows ? allocate_shadow_layer() : -1;
            GpuDirectionalLight gpu{};
            gpu.direction_shadow_layer = glm::vec4(light.direction, static_cast<float>(layer));
            gpu.color_intensity        = glm::vec4(light.color, light.intensity);
            m_directional.push_back(gpu);

            if (layer >= 0)
            {
                ShadowCaster caster{};
                caster.type      = ShadowCaster::Type::Directional;
                caster.layer     = static_cast<uint32_t>(layer);
                caster.direction = light.direction;
                m_shadow_casters.push_back(caster);
            }
        }

        m_point.reserve(scene.point_lights.size());
        for (const RenderPointLight &light : scene.point_lights)
        {
            const int32_t layer = light.cast_shadows ? allocate_shadow_layer() : -1;
            GpuPointLight gpu{};
            gpu.position_intensity = glm::vec4(light.position, light.intensity);
            gpu.color_range        = glm::vec4(light.color, light.range);
            gpu.attenuation_shadow = glm::vec4(light.attenuation_constant,
                                               light.attenuation_linear,
                                               light.attenuation_quadratic,
                                               static_cast<float>(layer));
            m_point.push_back(gpu);

            if (layer >= 0)
            {
                ShadowCaster caster{};
                caster.type     = ShadowCaster::Type::Point;
                caster.layer    = static_cast<uint32_t>(layer);
                caster.position = light.position;
                m_shadow_casters.push_back(caster);
            }
        }

        m_area.reserve(scene.area_lights.size());
        for (const RenderAreaLight &light : scene.area_lights)
        {
            const int32_t layer = light.cast_shadows ? allocate_shadow_layer() : -1;
            GpuAreaLight gpu{};
            gpu.position_intensity    = glm::vec4(light.position, light.intensity);
            gpu.direction_size_x      = glm::vec4(light.direction, light.size.x);
            gpu.color_size_y          = glm::vec4(light.color, light.size.y);
            gpu.two_sided_shadow_pad  = glm::vec4(light.two_sided ? 1.0f : 0.0f,
                                                  static_cast<float>(layer),
                                                  0.0f,
                                                  0.0f);
            m_area.push_back(gpu);

            if (layer >= 0)
            {
                ShadowCaster caster{};
                caster.type      = ShadowCaster::Type::Area;
                caster.layer     = static_cast<uint32_t>(layer);
                caster.position  = light.position;
                caster.direction = light.direction;
                caster.size      = light.size;
                caster.two_sided = light.two_sided ? 1u : 0u;
                m_shadow_casters.push_back(caster);
            }
        }

        UploadBuffer(m_directional_buffer,
                     m_directional.data(),
                     m_directional.size() * sizeof(GpuDirectionalLight),
                     m_directional_capacity_bytes);
        UploadBuffer(m_point_buffer,
                     m_point.data(),
                     m_point.size() * sizeof(GpuPointLight),
                     m_point_capacity_bytes);
        UploadBuffer(m_area_buffer,
                     m_area.data(),
                     m_area.size() * sizeof(GpuAreaLight),
                     m_area_capacity_bytes);
        return true;
    }

    void LightStore::BindSsbos() const
    {
        if (m_directional_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_directional_lights, m_directional_buffer.Id());
        }
        if (m_point_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_point_lights, m_point_buffer.Id());
        }
        if (m_area_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_area_lights, m_area_buffer.Id());
        }
    }

    void LightStore::Clear()
    {
        m_shadow_casters.clear();
        m_directional.clear();
        m_point.clear();
        m_area.clear();

        m_directional_buffer.Destroy();
        m_point_buffer.Destroy();
        m_area_buffer.Destroy();

        m_directional_capacity_bytes = 0;
        m_point_capacity_bytes = 0;
        m_area_capacity_bytes = 0;
        m_initialized = false;
    }

} // namespace hybrid::renderer
