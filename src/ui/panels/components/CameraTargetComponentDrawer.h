#pragma once

#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"

namespace hybrid::ui
{

    void DrawCameraTargetComponent(entt::entity entity,
                                   const core::scene::CameraTargetComponent &component,
                                   CommandBuffer *commands);

} // namespace hybrid::ui
