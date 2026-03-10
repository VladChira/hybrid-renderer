#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace hybrid::core::scene
{

    struct Aabb
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
        bool valid = false;
    };

    struct Transform
    {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

} // namespace hybrid::core::scene
