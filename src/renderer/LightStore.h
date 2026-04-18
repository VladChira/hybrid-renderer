#pragma once

#include "renderer/RendererTypes.h"
#include "renderer/opengl/GLBuffer.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace hybrid::renderer
{

    // `shadow_layer` (stored as float in the .w slot of the first vec4) is
    // the index into the shadow mask texture array for this light, or -1 if
    // the light is not shadow-cast.
    struct GpuDirectionalLight
    {
        glm::vec4 direction_shadow_layer;  // xyz = direction (unit, light→surface), w = shadow layer (-1 = none)
        glm::vec4 color_intensity;         // xyz = color, w = intensity
    };
    static_assert(sizeof(GpuDirectionalLight) == 32, "GpuDirectionalLight must match std430 layout");

    struct GpuPointLight
    {
        glm::vec4 position_intensity;     // xyz = position, w = intensity
        glm::vec4 color_range;            // xyz = color,    w = range (0 = unbounded)
        glm::vec4 attenuation_shadow;     // x/y/z = c/l/q,  w = shadow layer (-1 = none)
    };
    static_assert(sizeof(GpuPointLight) == 48, "GpuPointLight must match std430 layout");

    struct GpuAreaLight
    {
        glm::vec4 position_intensity;   // xyz = position, w = intensity
        glm::vec4 direction_size_x;     // xyz = normal,   w = size.x
        glm::vec4 color_size_y;         // xyz = color,    w = size.y
        glm::vec4 two_sided_shadow_pad; // x = two_sided,  y = shadow layer (-1 = none), z/w = 0
    };
    static_assert(sizeof(GpuAreaLight) == 64, "GpuAreaLight must match std430 layout");

    // One dispatch is issued per ShadowCaster by the shadow pass. Parameters
    // are carried inline so the pass does not need to walk the light SSBOs
    // again.
    struct ShadowCaster
    {
        enum class Type : uint32_t
        {
            Directional = 0,
            Point       = 1,
            Area        = 2
        };
        Type      type        = Type::Directional;
        uint32_t  layer       = 0;
        glm::vec3 direction   {0.0f};
        glm::vec3 position    {0.0f};
        glm::vec2 size        {1.0f};
        uint32_t  two_sided   = 0;
    };

    class LightStore
    {
    public:
        LightStore();

        bool Init();
        // `enable_shadows` gates whether shadow-casting lights get a shadow
        // mask layer assignment. When false, every light's `shadow_layer` is
        // -1 and the deferred shader treats it as unshadowed (mask = 1.0).
        bool Update(const FrameSceneData &scene, bool enable_shadows);
        void BindSsbos() const;
        void Clear();

        uint32_t DirectionalCount() const { return static_cast<uint32_t>(m_directional.size()); }
        uint32_t PointCount()       const { return static_cast<uint32_t>(m_point.size()); }
        uint32_t AreaCount()        const { return static_cast<uint32_t>(m_area.size()); }

        const std::vector<ShadowCaster> &ShadowCasters() const { return m_shadow_casters; }

    private:
        std::vector<GpuDirectionalLight> m_directional;
        std::vector<GpuPointLight>       m_point;
        std::vector<GpuAreaLight>        m_area;
        std::vector<ShadowCaster>        m_shadow_casters;

        GLBuffer m_directional_buffer{};
        GLBuffer m_point_buffer{};
        GLBuffer m_area_buffer{};

        size_t m_directional_capacity_bytes = 0;
        size_t m_point_capacity_bytes       = 0;
        size_t m_area_capacity_bytes        = 0;

        bool m_initialized = false;
    };

} // namespace hybrid::renderer
