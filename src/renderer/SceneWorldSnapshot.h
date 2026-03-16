#pragma once

#include "core/scene/SceneWorld.h"
#include "renderer/RendererTypes.h"

namespace hybrid::renderer
{

    RenderSceneSnapshot BuildRenderSceneSnapshot(const core::scene::SceneWorld &scene_world);

} // namespace hybrid::renderer
