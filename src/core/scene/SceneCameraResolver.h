#pragma once

#include "core/scene/SceneWorld.h"

#include <glm/glm.hpp>

namespace hybrid::core::scene
{

    struct SceneCameraView
    {
        glm::mat4 view{1.0f};
        glm::mat4 projection{1.0f};
        glm::vec3 position{0.0f};
        float near_plane = 0.1f;
        float far_plane = 1000.0f;
        bool valid = false;
    };

    SceneCameraView ResolvePrimaryCameraView(SceneWorld &scene_world, float aspect_ratio);

} // namespace hybrid::core::scene

