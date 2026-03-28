#pragma once

#include "core/scene/SceneWorld.h"
#include "renderer/RendererTypes.h"

namespace hybrid::renderer
{

    FrameSceneData BuildFrameSceneData(const core::scene::SceneWorld &scene_world);

} // namespace hybrid::renderer
