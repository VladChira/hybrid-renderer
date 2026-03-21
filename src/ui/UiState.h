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

    struct UiState
    {
        const core::scene::SceneWorld *scene_world = nullptr;
        std::vector<UiMaterialEntry> materials;
        uint64_t viewport_color_texture = 0;
        renderer::RenderExtent viewport_render_extent{};
        renderer::RenderView viewport_render_view{};
        bool viewport_render_view_valid = false;
    };

} // namespace hybrid::ui
