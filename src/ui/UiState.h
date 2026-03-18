#pragma once

#include <cstdint>

namespace hybrid::core::scene
{
    class SceneWorld;
}

namespace hybrid::ui
{

    struct UiState
    {
        const core::scene::SceneWorld *scene_world = nullptr;
        uint64_t viewport_color_texture = 0;
    };

} // namespace hybrid::ui
