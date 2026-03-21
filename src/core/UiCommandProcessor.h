#pragma once

#include "assets/AssetManager.h"
#include "ui/UiCommands.h"

namespace hybrid::core
{

    void ProcessUiCommands(const ui::CommandBuffer &commands,
                           assets::AssetManager &assets,
                           assets::AssetId active_scene,
                           bool &should_quit);

} // namespace hybrid::core
