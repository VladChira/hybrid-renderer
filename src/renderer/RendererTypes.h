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

    struct RenderSettings
    {
        RenderMode mode = RenderMode::Lit;
        RenderExtent render_extent{1920, 1080};
        bool show_bounds = false;
        float exposure = 1.0f;
    };

    struct RenderMeshInstance
    {
        uint64_t instance_id = 0;
        assets::AssetHandle<core::scene::MeshAsset> mesh;
        glm::mat4 world_from_local{1.0f};
        core::scene::Aabb world_bounds{};
    };

    struct FrameSceneData
    {
        std::vector<RenderMeshInstance> opaque_mesh_instances;
        std::vector<RenderMeshInstance> masked_mesh_instances;
        std::vector<RenderMeshInstance> blended_mesh_instances;
        core::scene::Aabb scene_bounds{};
    };

    struct RenderSceneSnapshot
    {
        std::vector<RenderMeshInstance> mesh_instances;
    };

    struct RendererStats
    {
        uint32_t submitted_mesh_instances = 0;
        uint32_t submitted_primitives = 0;
        uint64_t submitted_vertices = 0;
        uint64_t submitted_triangles = 0;
        double cpu_frame_ms = 0.0;
    };

    struct RendererOutputHandle
    {
        uint64_t value = 0;
        bool IsValid() const { return value != 0; }
    };

    struct RendererOutputs
    {
        RendererOutputHandle color;
        RendererOutputHandle depth;
    };

} // namespace hybrid::renderer
