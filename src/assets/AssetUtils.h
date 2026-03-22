#pragma once

#include "assets/AssimpTextureCache.h"
#include "core/scene/SceneWorld.h"

#include <assimp/scene.h>

#include <string_view>

#include <glm/glm.hpp>

namespace hybrid::assets
{

    bool FillMaterialTexture(const aiMaterial &material,
                             aiTextureType type,
                             hybrid::core::scene::TextureColorSpace color_space,
                             AssimpTextureCache &texture_cache,
                             hybrid::core::scene::MaterialTexture &out_texture);

    inline hybrid::core::scene::Transform ToTransform(const aiMatrix4x4 &matrix)
    {
        aiVector3D scaling;
        aiQuaternion rotation;
        aiVector3D translation;
        matrix.Decompose(scaling, rotation, translation);

        hybrid::core::scene::Transform result{};
        result.translation = {translation.x, translation.y, translation.z};
        result.rotation = {rotation.w, rotation.x, rotation.y, rotation.z};
        result.scale = {scaling.x, scaling.y, scaling.z};
        return result;
    }

    inline hybrid::core::scene::TextureWrap ToWrap(aiTextureMapMode mode)
    {
        switch (mode)
        {
        case aiTextureMapMode_Clamp:
            return hybrid::core::scene::TextureWrap::ClampToEdge;
        case aiTextureMapMode_Mirror:
            return hybrid::core::scene::TextureWrap::MirroredRepeat;
        case aiTextureMapMode_Wrap:
        default:
            return hybrid::core::scene::TextureWrap::Repeat;
        }
    }

    inline bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }
        for (size_t i = 0; i < lhs.size(); ++i)
        {
            char a = lhs[i];
            char b = rhs[i];
            if (a == b)
            {
                continue;
            }
            if (a >= 'A' && a <= 'Z')
            {
                a = static_cast<char>(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z')
            {
                b = static_cast<char>(b - 'A' + 'a');
            }
            if (a != b)
            {
                return false;
            }
        }
        return true;
    }

    inline glm::vec3 ToVec3(const aiColor3D &color)
    {
        return {color.r, color.g, color.b};
    }

    inline glm::vec3 ToVec3(const aiVector3D &value)
    {
        return {value.x, value.y, value.z};
    }

    inline glm::vec3 NormalizeOrDefault(const glm::vec3 &value, const glm::vec3 &fallback)
    {
        const float len2 = glm::dot(value, value);
        if (len2 <= 1e-8f)
        {
            return fallback;
        }
        return value / std::sqrt(len2);
    }

}