#include "renderer/stores/LightStore.h"

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

    bool LightStore::Update(const FrameSceneData &scene)
    {
        if (!m_initialized)
        {
            return false;
        }

        m_directional.clear();
        m_directional.reserve(scene.directional_lights.size());
        for (const RenderDirectionalLight &light : scene.directional_lights)
        {
            GpuDirectionalLight gpu{};
            gpu.direction_cast_shadows = glm::vec4(light.direction, light.cast_shadows ? 1.0f : 0.0f);
            gpu.color_intensity = glm::vec4(light.color, light.intensity);
            m_directional.push_back(gpu);
        }

        m_point.clear();
        m_point.reserve(scene.point_lights.size());
        for (const RenderPointLight &light : scene.point_lights)
        {
            GpuPointLight gpu{};
            gpu.position_intensity = glm::vec4(light.position, light.intensity);
            gpu.color_range        = glm::vec4(light.color, light.range);
            gpu.attenuation_cast   = glm::vec4(light.attenuation_constant,
                                               light.attenuation_linear,
                                               light.attenuation_quadratic,
                                               light.cast_shadows ? 1.0f : 0.0f);
            m_point.push_back(gpu);
        }

        m_area.clear();
        m_area.reserve(scene.area_lights.size());
        for (const RenderAreaLight &light : scene.area_lights)
        {
            GpuAreaLight gpu{};
            gpu.position_intensity  = glm::vec4(light.position, light.intensity);
            gpu.direction_size_x    = glm::vec4(light.direction, light.size.x);
            gpu.color_size_y        = glm::vec4(light.color, light.size.y);
            gpu.two_sided_cast_pad  = glm::vec4(light.two_sided ? 1.0f : 0.0f,
                                                light.cast_shadows ? 1.0f : 0.0f,
                                                0.0f,
                                                0.0f);
            m_area.push_back(gpu);
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
