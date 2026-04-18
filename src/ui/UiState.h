#pragma once

#include "renderer/RendererTypes.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace hybrid::core::scene
{
    struct MaterialAsset;
    class SceneWorld;
}

namespace hybrid::ui
{

    enum class UiViewportVisualization : uint8_t
    {
        FinalColor = 0,
        GBufferRt0 = 1,
        GBufferRt1 = 2,
        GBufferDepth = 3,
        GBufferEntityId = 4
    };

    struct UiMaterialEntry
    {
        uint64_t asset_id = 0;
        std::string name;
        const core::scene::MaterialAsset *material = nullptr;
    };

    struct UiSelection
    {
        enum class Type
        {
            None,
            Entity,
            Material
        };

        Type type = Type::None;
        entt::entity entity = entt::null;
        uint64_t material_asset_id = 0;

        void Clear() noexcept
        {
            type = Type::None;
            entity = entt::null;
            material_asset_id = 0;
        }
    };

    enum class TransformTool : uint8_t
    {
        Translate = 0,
        Rotate,
        Scale
    };

    struct UiState
    {
        const core::scene::SceneWorld *scene_world = nullptr;
        const std::vector<UiMaterialEntry> *materials = nullptr;
        uint64_t viewport_color_texture = 0;
        uint64_t viewport_gbuffer_rt0_texture = 0;
        uint64_t viewport_gbuffer_rt1_texture = 0;
        uint64_t viewport_gbuffer_depth_texture = 0;
        uint64_t viewport_entity_id_texture = 0;
        UiViewportVisualization viewport_visualization = UiViewportVisualization::FinalColor;
        renderer::RenderExtent viewport_render_extent{};
        renderer::RenderView viewport_render_view{};
        bool viewport_render_view_valid = false;
        renderer::RenderSettings *render_settings = nullptr;
    };

} // namespace hybrid::ui
