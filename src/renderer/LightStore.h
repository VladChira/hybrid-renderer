#pragma once

#include "renderer/RendererTypes.h"
#include "renderer/opengl/GLBuffer.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace hybrid::renderer
{

    struct GpuDirectionalLight
    {
        glm::vec4 direction_cast_shadows;  // xyz = direction (unit, light→surface), w = cast_shadows (0/1)
        glm::vec4 color_intensity;         // xyz = color, w = intensity
    };
    static_assert(sizeof(GpuDirectionalLight) == 32, "GpuDirectionalLight must match std430 layout");

    struct GpuPointLight
    {
        glm::vec4 position_intensity;   // xyz = position, w = intensity
        glm::vec4 color_range;          // xyz = color,    w = range (0 = unbounded)
        glm::vec4 attenuation_cast;     // x/y/z = c/l/q,  w = cast_shadows (0/1)
    };
    static_assert(sizeof(GpuPointLight) == 48, "GpuPointLight must match std430 layout");

    struct GpuAreaLight
    {
        glm::vec4 position_intensity;   // xyz = position, w = intensity
        glm::vec4 direction_size_x;     // xyz = normal,   w = size.x
        glm::vec4 color_size_y;         // xyz = color,    w = size.y
        glm::vec4 two_sided_cast_pad;   // x = two_sided,  y = cast_shadows, z/w = 0
    };
    static_assert(sizeof(GpuAreaLight) == 64, "GpuAreaLight must match std430 layout");

    class LightStore
    {
    public:
        LightStore();

        bool Init();
        bool Update(const FrameSceneData &scene);
        void BindSsbos() const;
        void Clear();

        uint32_t DirectionalCount() const { return static_cast<uint32_t>(m_directional.size()); }
        uint32_t PointCount()       const { return static_cast<uint32_t>(m_point.size()); }
        uint32_t AreaCount()        const { return static_cast<uint32_t>(m_area.size()); }

    private:
        std::vector<GpuDirectionalLight> m_directional;
        std::vector<GpuPointLight>       m_point;
        std::vector<GpuAreaLight>        m_area;

        GLBuffer m_directional_buffer{};
        GLBuffer m_point_buffer{};
        GLBuffer m_area_buffer{};

        size_t m_directional_capacity_bytes = 0;
        size_t m_point_capacity_bytes       = 0;
        size_t m_area_capacity_bytes        = 0;

        bool m_initialized = false;
    };

} // namespace hybrid::renderer
