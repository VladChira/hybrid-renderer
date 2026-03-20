#pragma once

#include "ui/UiState.h"

namespace hybrid::core::scene
{
    class SceneWorld;
}

namespace hybrid::ui
{

    void BuildMaterialEntries(const core::scene::SceneWorld *scene_world, std::vector<UiMaterialEntry> &out_entries);

} // namespace hybrid::ui
