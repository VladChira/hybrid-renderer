#pragma once

#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"

namespace hybrid::ui
{

    void DrawHdriLightComponent(entt::entity entity,
                                const core::scene::HdriLightComponent &component,
                                CommandBuffer *commands);

} // namespace hybrid::ui
