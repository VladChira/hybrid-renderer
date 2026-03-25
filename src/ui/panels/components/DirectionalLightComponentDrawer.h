#pragma once

#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"

namespace hybrid::ui
{

    void DrawDirectionalLightComponent(entt::entity entity,
                                       const core::scene::DirectionalLightComponent &component,
                                       CommandBuffer *commands);

} // namespace hybrid::ui
