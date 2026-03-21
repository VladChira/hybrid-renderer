#pragma once

#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"

namespace hybrid::core::scene
{
    struct MaterialAsset;
}

namespace hybrid::ui
{

    void DrawMaterialComponent(const core::scene::MaterialAsset &material, uint64_t material_id, CommandBuffer *commands);

} // namespace hybrid::ui
