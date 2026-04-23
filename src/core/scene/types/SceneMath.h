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

    inline Aabb EmptyAabb()
    {
        core::scene::Aabb a{};
        a.min = glm::vec3(std::numeric_limits<float>::infinity());
        a.max = glm::vec3(-std::numeric_limits<float>::infinity());
        a.valid = false;
        return a;
    }

    inline void ExpandAabbToInclude(core::scene::Aabb &a, const core::scene::Aabb &b)
        {
            if (!b.valid)
            {
                return;
            }
            a.min = glm::min(a.min, b.min);
            a.max = glm::max(a.max, b.max);
            a.valid = true;
        }

    inline void ExpandAabbToInclude(core::scene::Aabb &a, const glm::vec3 &p)
    {
        a.min = glm::min(a.min, p);
        a.max = glm::max(a.max, p);
        a.valid = true;
    }


    struct Transform
    {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

} // namespace hybrid::core::scene
