#pragma once

#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"

namespace hybrid::ui
{

    void DrawNameComponent(entt::entity entity,
                           const core::scene::NameComponent &component,
                           CommandBuffer *commands);

} // namespace hybrid::ui
