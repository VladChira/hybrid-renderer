#include "renderer/passes/HdriPrecomputePass.h"

#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLTexture.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace hybrid::renderer
{
    namespace
    {
        struct IBLCacheKey
        {
            uint64_t light_id = 0;
            uint64_t hdri_texture_id = 0;

            bool operator==(const IBLCacheKey &other) const noexcept
            {
                return light_id == other.light_id &&
                       hdri_texture_id == other.hdri_texture_id;
            }
        };

        struct IBLCacheKeyHash
        {
            static size_t HashCombine(size_t seed, size_t value) noexcept
            {
                return seed ^ (value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u));
            }

            size_t operator()(const IBLCacheKey &key) const noexcept
            {
                size_t h1 = std::hash<uint64_t>{}(key.light_id);
                size_t h2 = std::hash<uint64_t>{}(key.hdri_texture_id);

                size_t seed = 0;
                seed = HashCombine(seed, h1);
                seed = HashCombine(seed, h2);
                return seed;
            }
        };

        enum class IBLBakeState
        {
            Pending,
            Baking,
            Ready,
            Failed
        };

        struct IBLCached
        {
            IBLBakeState state = IBLBakeState::Pending;
            uint64_t last_bake_settings_hash = 0;
            uint64_t last_used_frame = 0;

            GLTexture environment_cubemap{GL_TEXTURE_CUBE_MAP};
            GLTexture irradiance_cubemap{GL_TEXTURE_CUBE_MAP};
            GLTexture prefiltered_specular_cubemap{GL_TEXTURE_CUBE_MAP};
            GLTexture brdf_lut{GL_TEXTURE_2D};
        };
    } // namespace

    struct HdriPrecomputePass::Impl
    {
        std::unordered_map<IBLCacheKey, IBLCached, IBLCacheKeyHash> ibl_cache{};
    };

    HdriPrecomputePass::HdriPrecomputePass(GLShaderProgram *equirect_to_cubemap_shader,
                                           GLShaderProgram *irradiance_convolution)
        : m_equirect_to_cubemap_shader(equirect_to_cubemap_shader),
          m_irradiance_convolution(irradiance_convolution),
          m_impl(std::make_unique<Impl>())
    {
    }

    HdriPrecomputePass::~HdriPrecomputePass() = default;

    const char *HdriPrecomputePass::Name() const
    {
        return "HdriPrecompute";
    }

    bool HdriPrecomputePass::Execute(const HdriPrecomputePassInput &input, HdriPrecomputePassOutput &output)
    {
        (void)output;

        if (m_impl == nullptr || input.scene_data == nullptr)
        {
            return false;
        }

        // TODO: perform equirect->cubemap, irradiance/specular precompute, and BRDF LUT generation.
        return true;
    }

} // namespace hybrid::renderer
