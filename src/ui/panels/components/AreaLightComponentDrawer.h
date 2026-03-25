#pragma once

#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"

namespace hybrid::ui
{

    void DrawAreaLightComponent(entt::entity entity,
                                const core::scene::AreaLightComponent &component,
                                CommandBuffer *commands);

} // namespace hybrid::ui
