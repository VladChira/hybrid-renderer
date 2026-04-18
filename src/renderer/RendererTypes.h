#pragma once

#include "core/scene/types/SceneAssets.h"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace hybrid::renderer
{

    struct RenderExtent
    {
        uint32_t width = 0;
        uint32_t height = 0;

        bool IsValid() const { return width > 0 && height > 0; }
    };

    struct FrameContext
    {
        uint64_t frame_index = 0;
        float delta_seconds = 0.0f;
        double time_seconds = 0.0;
        RenderExtent render_extent{};
    };

    struct RenderView
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 position{0.0f};
        float near_plane = 0.1f;
        float far_plane = 1000.0f;
    };

    enum class RenderMode
    {
        Lit,
        Unlit,
        Wireframe
    };

    enum class ToneMapper
    {
        Legacy = 0,
        ACES = 1
    };

    struct RenderSettings
    {
        RenderMode mode = RenderMode::Lit;
        RenderExtent render_extent{1920, 1080};
        bool show_bounds = false;
        float exposure = 1.0f;
        ToneMapper tone_mapper = ToneMapper::ACES;
        float legacy_curve_strength = 1.0f;
        float legacy_gamma = 2.2f;
        float aces_input_scale = 2.0f;
        float aces_saturation = 1.0f;

        // HDRI precompute controls (diffuse/specular IBL assets).
        uint32_t hdri_env_cubemap_size = 512;
        uint32_t hdri_irradiance_cubemap_size = 32;
        uint32_t hdri_prefilter_cubemap_size = 128;
        uint32_t hdri_prefilter_mip_levels = 5;
        uint32_t hdri_brdf_lut_size = 512;

        // Ray tracing diagnostics.
        bool  enable_raytrace_heatmap    = false;  // set each frame by the UI when the heatmap target is displayed
        float raytrace_heatmap_scale     = 256.0f; // divisor applied to visit counts before LUT sampling
        bool  enable_raytrace_albedo     = false;  // set each frame by the UI when the albedo target is displayed

        // Ray-traced shadows (Phase 3).
        bool  enable_raytrace_shadows        = true;
        float raytrace_shadow_normal_bias    = 0.02f;

        // Shadow denoising (Phase 3, temporal + à-trous).
        bool  enable_shadow_denoise          = true;
        float shadow_denoise_temporal_alpha  = 0.92f;
        int   shadow_denoise_iterations      = 3;
        float shadow_denoise_depth_sigma     = 0.01f;
        float shadow_denoise_normal_sigma    = 32.0f;

        // Screen-space global illumination with BVH fallback.
        bool  enable_ssgi                    = true;
        float ssgi_intensity                 = 1.0f;
        float ssgi_max_ray_distance          = 30.0f;
        float ssgi_screen_thickness          = 0.01f;
        bool  enable_ssgi_denoise            = true;
        float ssgi_denoise_temporal_alpha    = 0.92f;
        int   ssgi_denoise_iterations        = 3;
        float ssgi_denoise_depth_sigma       = 0.02f;
        float ssgi_denoise_normal_sigma      = 32.0f;
    };

    struct RenderMeshInstance
    {
        uint64_t instance_id = 0;
        assets::AssetHandle<core::scene::MeshAsset> mesh;
        glm::mat4 world_from_local{1.0f};
        core::scene::Aabb world_bounds{};
    };

    struct RenderDirectionalLight
    {
        uint64_t instance_id = 0;
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        bool cast_shadows = true;
    };

    struct RenderPointLight
    {
        uint64_t instance_id = 0;
        glm::vec3 position{0.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float range = 0.0f;
        float attenuation_constant = 1.0f;
        float attenuation_linear = 0.0f;
        float attenuation_quadratic = 1.0f;
        bool cast_shadows = true;
    };

    struct RenderAreaLight
    {
        uint64_t instance_id = 0;
        glm::vec3 position{0.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        glm::vec2 size{1.0f, 1.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        bool two_sided = false;
        bool cast_shadows = true;
        bool visible = false;
    };

    struct RenderHdriLight
    {
        uint64_t instance_id = 0;
        float intensity = 1.0f;
        float yaw_radians = 0.0f;
        bool cast_shadows = false;
        assets::AssetHandle<assets::ImageAsset> texture;
    };

    struct FrameSceneData
    {
        std::vector<RenderMeshInstance> opaque_mesh_instances;
        std::vector<RenderMeshInstance> masked_mesh_instances;
        std::vector<RenderMeshInstance> blended_mesh_instances;
        std::vector<RenderDirectionalLight> directional_lights;
        std::vector<RenderPointLight> point_lights;
        std::vector<RenderAreaLight> area_lights;
        std::vector<RenderHdriLight> hdri_lights;
        core::scene::Aabb scene_bounds{};
    };

    struct RenderSceneSnapshot
    {
        std::vector<RenderMeshInstance> mesh_instances;
        std::vector<RenderDirectionalLight> directional_lights;
        std::vector<RenderPointLight> point_lights;
        std::vector<RenderAreaLight> area_lights;
        std::vector<RenderHdriLight> hdri_lights;
    };

    struct RendererStats
    {
        struct GBufferStats
        {
            uint32_t draw_calls = 0;
            uint32_t uniform_updates = 0;
            uint32_t texture_binds = 0;
            uint32_t primitive_cache_misses = 0;
            uint32_t texture_cache_misses = 0;
            uint32_t primitive_uploads = 0;
            uint32_t texture_uploads = 0;
        };

        uint32_t submitted_mesh_instances = 0;
        uint32_t submitted_primitives = 0;
        uint64_t submitted_vertices = 0;
        uint64_t submitted_triangles = 0;
        GBufferStats gbuffer{};
        double cpu_frame_ms = 0.0;
    };

    using GlTextureId = uint32_t;

    struct RendererOutputs
    {
        GlTextureId color = 0;
        GlTextureId depth = 0;
        GlTextureId gbuffer_rt0 = 0;
        GlTextureId gbuffer_rt1 = 0;
        GlTextureId gbuffer_entity_id = 0;
        GlTextureId raytrace_heatmap = 0;
        GlTextureId raytrace_albedo = 0;
    };

} // namespace hybrid::renderer
