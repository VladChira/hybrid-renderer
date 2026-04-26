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

    enum class SkyboxTextureSource
    {
        HdriImage = 0,
        IrradianceMap = 1,
        PrefilterMap = 2
    };

    struct RenderSettings
    {
        RenderMode mode = RenderMode::Lit;
        RenderExtent render_extent{1920, 1080};
        bool show_bounds = false;

        bool compute_bvh_heatmap = false;
        float bvh_heatmap_scale = 256.0f;
        bool enable_ray_traced_shadows = true;
        float raytrace_shadow_normal_bias = 0.02f;

        float exposure = 1.0f;
        ToneMapper tone_mapper = ToneMapper::ACES;
        float legacy_curve_strength = 1.0f;
        float legacy_gamma = 2.2f;
        float aces_input_scale = 2.0f;
        float aces_saturation = 1.0f;
        bool draw_hdri_as_skybox = true;
        SkyboxTextureSource skybox_texture_source = SkyboxTextureSource::HdriImage;

        // HDRI precompute controls (diffuse/specular IBL assets).
        uint32_t hdri_env_cubemap_size = 512;
        uint32_t hdri_irradiance_cubemap_size = 32;
        uint32_t hdri_prefilter_cubemap_size = 128;
        uint32_t hdri_prefilter_mip_levels = 5;
        uint32_t hdri_brdf_lut_size = 512;
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
        glm::vec3 right{1.0f, 0.0f, 0.0f};
        glm::vec2 size{1.0f, 1.0f};
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        bool two_sided = false;
        bool visible = true;
        bool cast_shadows = true;
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

    struct RenderChannelOutputs
    {
        GlTextureId rgb = 0;
        GlTextureId r = 0;
        GlTextureId g = 0;
        GlTextureId b = 0;
        GlTextureId a = 0;
    };

    struct RendererOutputs
    {
        GlTextureId color = 0;
        GlTextureId depth = 0;
        GlTextureId gbuffer_rt0 = 0;
        GlTextureId gbuffer_rt1 = 0;
        GlTextureId gbuffer_entity_id = 0;
        GlTextureId raytrace_heatmap = 0;
        RenderChannelOutputs color_channels{};
        RenderChannelOutputs gbuffer_rt0_channels{};
        RenderChannelOutputs gbuffer_rt1_channels{};
    };

} // namespace hybrid::renderer
