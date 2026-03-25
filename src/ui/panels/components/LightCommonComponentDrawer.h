#pragma once

#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"

namespace hybrid::ui
{

    void DrawLightCommonComponent(entt::entity entity,
                                  const core::scene::LightCommonComponent &component,
                                  CommandBuffer *commands);

} // namespace hybrid::ui
