#include "renderer/MaterialStore.h"

#include "core/Log.h"
#include "renderer/ShaderBindings.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace hybrid::renderer
{

    namespace
    {
        constexpr size_t kMinCapacityBytes = 4 * 1024;

        size_t GrowCapacityBytes(size_t needed_bytes, size_t current_capacity_bytes)
        {
            size_t target = std::max(current_capacity_bytes * 2, kMinCapacityBytes);
            while (target < needed_bytes)
            {
                target *= 2;
            }
            return target;
        }

        GLint ToGlWrap(const core::scene::TextureWrap wrap)
        {
            switch (wrap)
            {
            case core::scene::TextureWrap::MirroredRepeat:
                return GL_MIRRORED_REPEAT;
            case core::scene::TextureWrap::ClampToEdge:
                return GL_CLAMP_TO_EDGE;
            case core::scene::TextureWrap::Repeat:
            default:
                return GL_REPEAT;
            }
        }

        GLint ToGlMagFilter(const core::scene::TextureFilter filter)
        {
            return filter == core::scene::TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR;
        }

        GLint ToGlMinFilter(const core::scene::TextureFilter min_filter, const core::scene::MipFilter mip_filter)
        {
            if (mip_filter == core::scene::MipFilter::None)
            {
                return min_filter == core::scene::TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR;
            }

            const bool nearest_min = min_filter == core::scene::TextureFilter::Nearest;
            const bool nearest_mip = mip_filter == core::scene::MipFilter::Nearest;
            if (nearest_min && nearest_mip)
            {
                return GL_NEAREST_MIPMAP_NEAREST;
            }
            if (nearest_min && !nearest_mip)
            {
                return GL_NEAREST_MIPMAP_LINEAR;
            }
            if (!nearest_min && nearest_mip)
            {
                return GL_LINEAR_MIPMAP_NEAREST;
            }
            return GL_LINEAR_MIPMAP_LINEAR;
        }

        std::optional<std::pair<GLint, GLenum>> ResolveTextureFormats(const assets::ImageAsset &image,
                                                                      core::scene::TextureColorSpace color_space)
        {
            GLenum pixel_format = GL_RGB;
            switch (image.channels)
            {
            case 1: pixel_format = GL_RED; break;
            case 2: pixel_format = GL_RG; break;
            case 3: pixel_format = GL_RGB; break;
            case 4: pixel_format = GL_RGBA; break;
            default: return std::nullopt;
            }

            GLint internal_format = GL_RGBA8;
            if (image.is_hdr)
            {
                switch (image.channels)
                {
                case 1: internal_format = GL_R16F; break;
                case 2: internal_format = GL_RG16F; break;
                case 3: internal_format = GL_RGB16F; break;
                case 4: internal_format = GL_RGBA16F; break;
                default: return std::nullopt;
                }
            }
            else if (color_space == core::scene::TextureColorSpace::Srgb)
            {
                if (image.channels == 3) internal_format = GL_SRGB8;
                else if (image.channels == 4) internal_format = GL_SRGB8_ALPHA8;
                else if (image.channels == 2) internal_format = GL_RG8;
                else internal_format = GL_R8;
            }
            else
            {
                switch (image.channels)
                {
                case 1: internal_format = GL_R8; break;
                case 2: internal_format = GL_RG8; break;
                case 3: internal_format = GL_RGB8; break;
                case 4: internal_format = GL_RGBA8; break;
                default: return std::nullopt;
                }
            }

            return std::make_pair(internal_format, pixel_format);
        }
    } // namespace

    MaterialStore::MaterialStore() = default;

    bool MaterialStore::Init()
    {
        if (m_initialized)
        {
            return true;
        }

        if (!GLTexture::IsBindlessTextureSupported())
        {
            LOG_ERROR("[MaterialStore] GL_ARB_bindless_texture is not available; Phase 0 refactor requires bindless textures.");
            return false;
        }

        if (!m_material_buffer.Create(GL_SHADER_STORAGE_BUFFER))
        {
            LOG_ERROR("[MaterialStore] Failed to create material SSBO.");
            return false;
        }

        constexpr uint8_t kWhiteRgba[4]      = {255, 255, 255, 255};
        constexpr uint8_t kFlatNormalRgba[4] = {128, 128, 255, 255};
        constexpr uint8_t kBlackRgba[4]      = {0,   0,   0,   255};

        if (!CreateSolidTexture(m_white_texture, kWhiteRgba) ||
            !CreateSolidTexture(m_flat_normal_texture, kFlatNormalRgba) ||
            !CreateSolidTexture(m_black_texture, kBlackRgba))
        {
            LOG_ERROR("[MaterialStore] Failed to create default textures.");
            return false;
        }

        if (!m_white_texture.MakeBindlessResident() ||
            !m_flat_normal_texture.MakeBindlessResident() ||
            !m_black_texture.MakeBindlessResident())
        {
            LOG_ERROR("[MaterialStore] Failed to make default texture handles resident.");
            return false;
        }

        m_white_handle = m_white_texture.BindlessHandle();
        m_flat_normal_handle = m_flat_normal_texture.BindlessHandle();
        m_black_handle = m_black_texture.BindlessHandle();

        // Install the default material at slot 0.
        GpuMaterial default_material{};
        default_material.base_color_factor    = glm::vec4(1.0f);
        default_material.emissive_and_cutoff  = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
        default_material.scalar_factors       = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
        default_material.flags_texcoords      = glm::uvec4(0u, 0u, 0u, 0u);
        default_material.base_color_handle          = m_white_handle;
        default_material.metallic_roughness_handle  = m_white_handle;
        default_material.normal_handle              = m_flat_normal_handle;
        default_material.occlusion_handle           = m_white_handle;
        default_material.emissive_handle            = m_black_handle;

        m_materials.push_back(default_material);
        m_dirty = true;
        m_initialized = true;
        return true;
    }

    uint32_t MaterialStore::GetOrUploadMaterial(const assets::AssetHandle<core::scene::MaterialAsset> &handle)
    {
        if (!m_initialized)
        {
            return kDefaultMaterialIndex;
        }

        const core::scene::MaterialAsset *material = handle.Get();
        const uint64_t asset_id = handle.Id().value;
        if (material == nullptr || asset_id == 0)
        {
            return kDefaultMaterialIndex;
        }

        if (const auto it = m_material_index.find(asset_id); it != m_material_index.end())
        {
            return it->second;
        }

        GpuMaterial record{};
        record.base_color_factor = material->base_color_factor;
        record.emissive_and_cutoff = glm::vec4(material->emissive_factor, material->alpha_cutoff);
        record.scalar_factors = glm::vec4(material->metallic_factor,
                                          material->roughness_factor,
                                          material->normal_scale,
                                          material->occlusion_strength);

        uint32_t alpha_mode = 0u;
        switch (material->alpha_mode)
        {
        case core::scene::AlphaMode::Opaque: alpha_mode = 0u; break;
        case core::scene::AlphaMode::Mask:   alpha_mode = 1u; break;
        case core::scene::AlphaMode::Blend:  alpha_mode = 2u; break;
        }

        const auto texcoord_bit = [](int texcoord, uint32_t bit_position) -> uint32_t
        {
            return ((texcoord == 1) ? 1u : 0u) << bit_position;
        };

        uint32_t texcoord_bits = 0u;
        texcoord_bits |= texcoord_bit(material->base_color_texture.texcoord,          material_texcoord_bit::k_base_color);
        texcoord_bits |= texcoord_bit(material->metallic_roughness_texture.texcoord,  material_texcoord_bit::k_metallic_roughness);
        texcoord_bits |= texcoord_bit(material->normal_texture.texcoord,              material_texcoord_bit::k_normal);
        texcoord_bits |= texcoord_bit(material->occlusion_texture.texcoord,           material_texcoord_bit::k_occlusion);
        texcoord_bits |= texcoord_bit(material->emissive_texture.texcoord,            material_texcoord_bit::k_emissive);

        record.flags_texcoords = glm::uvec4(alpha_mode, texcoord_bits, 0u, 0u);

        record.base_color_handle         = ResolveTextureHandle(material->base_color_texture,         m_white_handle);
        record.metallic_roughness_handle = ResolveTextureHandle(material->metallic_roughness_texture, m_white_handle);
        record.normal_handle             = ResolveTextureHandle(material->normal_texture,             m_flat_normal_handle);
        record.occlusion_handle          = ResolveTextureHandle(material->occlusion_texture,          m_white_handle);
        record.emissive_handle           = ResolveTextureHandle(material->emissive_texture,           m_black_handle);

        const uint32_t index = static_cast<uint32_t>(m_materials.size());
        m_materials.push_back(record);
        m_material_index.emplace(asset_id, index);
        m_dirty = true;
        return index;
    }

    uint64_t MaterialStore::ResolveTextureHandle(const core::scene::MaterialTexture &texture, uint64_t fallback_handle)
    {
        if (!texture.image.IsValid())
        {
            return fallback_handle;
        }

        const uint64_t image_id = texture.image.Id().value;
        if (image_id == 0)
        {
            return fallback_handle;
        }

        if (const auto it = m_textures.find(image_id); it != m_textures.end())
        {
            return it->second.BindlessHandle() != 0 ? it->second.BindlessHandle() : fallback_handle;
        }

        GLTexture texture_gpu{GL_TEXTURE_2D};
        if (!UploadTextureToGpu(texture, texture_gpu))
        {
            return fallback_handle;
        }
        if (!texture_gpu.MakeBindlessResident())
        {
            return fallback_handle;
        }

        const uint64_t handle = texture_gpu.BindlessHandle();
        m_textures.emplace(image_id, std::move(texture_gpu));
        return handle;
    }

    bool MaterialStore::UploadTextureToGpu(const core::scene::MaterialTexture &texture, GLTexture &out_texture)
    {
        const assets::ImageAsset *image = texture.image.Get();
        if (image == nullptr || !image->IsValid())
        {
            return false;
        }

        const auto formats = ResolveTextureFormats(*image, texture.color_space);
        if (!formats.has_value())
        {
            return false;
        }

        out_texture.Bind();
        out_texture.SetParameter(GL_TEXTURE_WRAP_S, ToGlWrap(texture.sampler.wrap_s));
        out_texture.SetParameter(GL_TEXTURE_WRAP_T, ToGlWrap(texture.sampler.wrap_t));
        out_texture.SetParameter(GL_TEXTURE_MIN_FILTER, ToGlMinFilter(texture.sampler.min_filter, texture.sampler.mip_filter));
        out_texture.SetParameter(GL_TEXTURE_MAG_FILTER, ToGlMagFilter(texture.sampler.mag_filter));

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        out_texture.SetImage2D(0,
                               formats->first,
                               image->width,
                               image->height,
                               formats->second,
                               image->is_hdr ? GL_FLOAT : GL_UNSIGNED_BYTE,
                               image->is_hdr ? static_cast<const void *>(image->pixels_f32.data())
                                             : static_cast<const void *>(image->pixels.data()));
        if (texture.sampler.mip_filter != core::scene::MipFilter::None)
        {
            out_texture.GenerateMipmap();
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        return true;
    }

    bool MaterialStore::CreateSolidTexture(GLTexture &out_texture, const uint8_t rgba[4])
    {
        if (!out_texture.IsValid() && !out_texture.Create(GL_TEXTURE_2D))
        {
            return false;
        }
        out_texture.Bind();
        out_texture.SetParameter(GL_TEXTURE_WRAP_S, GL_REPEAT);
        out_texture.SetParameter(GL_TEXTURE_WRAP_T, GL_REPEAT);
        out_texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        out_texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        out_texture.SetImage2D(0, GL_RGBA8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        return true;
    }

    bool MaterialStore::Sync()
    {
        if (!m_initialized || !m_material_buffer.IsValid() || m_materials.empty())
        {
            return m_initialized;
        }
        if (!m_dirty)
        {
            return true;
        }

        const size_t needed_bytes = m_materials.size() * sizeof(GpuMaterial);
        m_material_buffer.Bind();
        if (needed_bytes > m_capacity_bytes)
        {
            m_capacity_bytes = GrowCapacityBytes(needed_bytes, m_capacity_bytes);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         static_cast<GLsizeiptr>(m_capacity_bytes),
                         nullptr,
                         GL_DYNAMIC_DRAW);
        }
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                        static_cast<GLsizeiptr>(needed_bytes),
                        m_materials.data());
        GLBuffer::Unbind(GL_SHADER_STORAGE_BUFFER);
        m_dirty = false;
        return true;
    }

    void MaterialStore::BindSsbo() const
    {
        if (m_material_buffer.IsValid())
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding::k_materials, m_material_buffer.Id());
        }
    }

    void MaterialStore::Clear()
    {
        // GLTexture destructors call MakeBindlessNonResident + glDeleteTextures;
        // clearing the map destroys them while the GL context is still alive.
        m_textures.clear();

        m_white_texture.Destroy();
        m_flat_normal_texture.Destroy();
        m_black_texture.Destroy();
        m_white_handle = 0;
        m_flat_normal_handle = 0;
        m_black_handle = 0;

        m_materials.clear();
        m_material_index.clear();

        m_material_buffer.Destroy();
        m_capacity_bytes = 0;
        m_dirty = false;
        m_initialized = false;
    }

} // namespace hybrid::renderer
