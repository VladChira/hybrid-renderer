#pragma once

namespace hybrid::core::scene
{
    class SceneWorld;
}

namespace hybrid::ui
{

    struct UiState
    {
        const core::scene::SceneWorld *scene_world = nullptr;
    };

} // namespace hybrid::ui
