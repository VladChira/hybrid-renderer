#pragma once

#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"

namespace hybrid::ui
{

    void DrawTransformComponent(entt::entity entity,
                                const core::scene::TransformComponent &component,
                                CommandBuffer *commands);

} // namespace hybrid::ui
