#pragma once

#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"

namespace hybrid::ui
{

    void DrawPointLightComponent(entt::entity entity,
                                 const core::scene::PointLightComponent &component,
                                 CommandBuffer *commands);

} // namespace hybrid::ui
