#pragma once

#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"

namespace hybrid::ui
{

    void DrawCameraComponent(entt::entity entity,
                             const core::scene::CameraComponent &component,
                             CommandBuffer *commands);

} // namespace hybrid::ui
