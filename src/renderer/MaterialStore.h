#pragma once

#include "assets/AssetManager.h"
#include "core/scene/types/SceneAssets.h"
#include "renderer/opengl/GLBuffer.h"
#include "renderer/opengl/GLTexture.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace hybrid::renderer
{

    // std430-compatible material record. All texture slots are bindless (uvec2
    // sampler handles); empty slots point at the store's default textures so
    // the fragment shader does not need branching per-slot.
    struct GpuMaterial
    {
        glm::vec4 base_color_factor;
        glm::vec4 emissive_and_cutoff;   // xyz = emissive, w = alpha_cutoff
        glm::vec4 scalar_factors;        // x = metallic, y = roughness, z = normal_scale, w = occlusion_strength
        glm::uvec4 flags_texcoords;      // x = alpha_mode (0/1/2), y = texcoord_bits (1 bit per slot), z/w = pad

        uint64_t base_color_handle;
        uint64_t metallic_roughness_handle;
        uint64_t normal_handle;
        uint64_t occlusion_handle;
        uint64_t emissive_handle;
        uint64_t _pad_handle;
        glm::vec4 _pad_tail;
    };
    static_assert(sizeof(GpuMaterial) == 128, "GpuMaterial must match std430 layout (128 bytes)");

    // Reserved material index used when a primitive does not reference a
    // valid MaterialAsset. Slot 0 in the SSBO is populated with a neutral
    // default by Init().
    constexpr uint32_t kDefaultMaterialIndex = 0;

    // Texcoord-bit positions in GpuMaterial::flags_texcoords.y.
    namespace material_texcoord_bit
    {
        constexpr uint32_t k_base_color = 0u;
        constexpr uint32_t k_metallic_roughness = 1u;
        constexpr uint32_t k_normal = 2u;
        constexpr uint32_t k_occlusion = 3u;
        constexpr uint32_t k_emissive = 4u;
    }

    class MaterialStore
    {
    public:
        MaterialStore();

        // Creates default textures (white/flat-normal/black), makes their
        // bindless handles resident, and installs a neutral material at slot 0.
        bool Init();

        // Returns the material table index to use for `handle`. Uploads and
        // caches textures + material record on first encounter. Returns
        // kDefaultMaterialIndex for invalid handles.
        uint32_t GetOrUploadMaterial(const assets::AssetHandle<core::scene::MaterialAsset> &handle);

        // Pushes pending SSBO updates to the GPU.
        bool Sync();

        void BindSsbo() const;
        void Clear();

        uint32_t MaterialCount() const { return static_cast<uint32_t>(m_materials.size()); }

    private:
        // Uploads `texture.image` (deduped by asset id) and returns its
        // resident bindless handle. Returns `fallback_handle` when the texture
        // has no image or upload fails.
        uint64_t ResolveTextureHandle(const core::scene::MaterialTexture &texture, uint64_t fallback_handle);

        bool UploadTextureToGpu(const core::scene::MaterialTexture &texture, GLTexture &out_texture);
        bool CreateSolidTexture(GLTexture &out_texture, const uint8_t rgba[4]);

        std::unordered_map<uint64_t, GLTexture> m_textures;
        std::unordered_map<uint64_t, uint32_t>  m_material_index;   // MaterialAsset id -> index

        GLTexture m_white_texture{};
        GLTexture m_flat_normal_texture{};
        GLTexture m_black_texture{};

        uint64_t m_white_handle = 0;
        uint64_t m_flat_normal_handle = 0;
        uint64_t m_black_handle = 0;

        std::vector<GpuMaterial> m_materials;
        GLBuffer m_material_buffer{};
        size_t m_capacity_bytes = 0;
        bool m_dirty = false;
        bool m_initialized = false;
    };

} // namespace hybrid::renderer
