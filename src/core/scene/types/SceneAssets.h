#pragma once

#include "assets/AssetManager.h"
#include "assets/ImageAsset.h"
#include "core/scene/types/SceneMath.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace hybrid::core::scene
{

    struct MaterialAsset;

    struct Vertex
    {
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f};
        glm::vec4 tangent{0.0f};
        glm::vec2 uv0{0.0f};
        glm::vec2 uv1{0.0f};
        glm::vec4 color0{1.0f};
    };

    struct MeshPrimitive
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        assets::AssetHandle<MaterialAsset> material;
        Aabb bounds{};
    };

    struct MeshAsset
    {
        std::string name;
        std::vector<MeshPrimitive> primitives;
        Aabb bounds{};
    };

    enum class AlphaMode
    {
        Opaque,
        Mask,
        Blend
    };

    enum class TextureFilter
    {
        Nearest,
        Linear
    };

    enum class MipFilter
    {
        None,
        Nearest,
        Linear
    };

    enum class TextureWrap
    {
        Repeat,
        MirroredRepeat,
        ClampToEdge
    };

    struct TextureSampler
    {
        TextureFilter min_filter = TextureFilter::Linear;
        TextureFilter mag_filter = TextureFilter::Linear;
        MipFilter mip_filter = MipFilter::Linear;
        TextureWrap wrap_s = TextureWrap::Repeat;
        TextureWrap wrap_t = TextureWrap::Repeat;
    };

    enum class TextureColorSpace
    {
        Linear,
        Srgb
    };

    struct MaterialTexture
    {
        std::string name;
        assets::AssetHandle<assets::ImageAsset> image;
        TextureSampler sampler{};
        int texcoord = 0;
        glm::vec2 offset{0.0f};
        glm::vec2 scale{1.0f};
        float rotation = 0.0f;
        bool has_transform = false;
        TextureColorSpace color_space = TextureColorSpace::Linear;
    };

    struct MaterialAsset
    {
        std::string name;

        glm::vec4 base_color_factor{1.0f};
        float metallic_factor = 1.0f;
        float roughness_factor = 1.0f;
        glm::vec3 emissive_factor{0.0f};

        AlphaMode alpha_mode = AlphaMode::Opaque;
        float alpha_cutoff = 0.5f;
        bool double_sided = false;

        MaterialTexture base_color_texture{};
        MaterialTexture metallic_roughness_texture{};
        MaterialTexture normal_texture{};
        MaterialTexture occlusion_texture{};
        MaterialTexture emissive_texture{};

        float normal_scale = 1.0f;
        float occlusion_strength = 1.0f;
    };

} // namespace hybrid::core::scene
