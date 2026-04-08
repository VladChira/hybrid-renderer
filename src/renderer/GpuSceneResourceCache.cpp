#include "renderer/GpuSceneResourceCache.h"

#include <cstddef>
#include <optional>
#include <utility>

namespace hybrid::renderer
{

    namespace
    {
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
            case 1:
                pixel_format = GL_RED;
                break;
            case 2:
                pixel_format = GL_RG;
                break;
            case 3:
                pixel_format = GL_RGB;
                break;
            case 4:
                pixel_format = GL_RGBA;
                break;
            default:
                return std::nullopt;
            }

            GLint internal_format = GL_RGBA8;
            if (image.is_hdr)
            {
                switch (image.channels)
                {
                case 1:
                    internal_format = GL_R16F;
                    break;
                case 2:
                    internal_format = GL_RG16F;
                    break;
                case 3:
                    internal_format = GL_RGB16F;
                    break;
                case 4:
                    internal_format = GL_RGBA16F;
                    break;
                default:
                    return std::nullopt;
                }
            }
            else if (color_space == core::scene::TextureColorSpace::Srgb)
            {
                if (image.channels == 3)
                {
                    internal_format = GL_SRGB8;
                }
                else if (image.channels == 4)
                {
                    internal_format = GL_SRGB8_ALPHA8;
                }
                else if (image.channels == 2)
                {
                    internal_format = GL_RG8;
                }
                else
                {
                    internal_format = GL_R8;
                }
            }
            else
            {
                switch (image.channels)
                {
                case 1:
                    internal_format = GL_R8;
                    break;
                case 2:
                    internal_format = GL_RG8;
                    break;
                case 3:
                    internal_format = GL_RGB8;
                    break;
                case 4:
                    internal_format = GL_RGBA8;
                    break;
                default:
                    return std::nullopt;
                }
            }

            return std::make_pair(internal_format, pixel_format);
        }

        bool UploadTextureToGpu(const core::scene::MaterialTexture &texture,
                                GpuSceneResourceCache::CachedTextureGpu &out_gpu)
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

            out_gpu.texture.Bind();
            out_gpu.texture.SetParameter(GL_TEXTURE_WRAP_S, ToGlWrap(texture.sampler.wrap_s));
            out_gpu.texture.SetParameter(GL_TEXTURE_WRAP_T, ToGlWrap(texture.sampler.wrap_t));
            out_gpu.texture.SetParameter(GL_TEXTURE_MIN_FILTER, ToGlMinFilter(texture.sampler.min_filter, texture.sampler.mip_filter));
            out_gpu.texture.SetParameter(GL_TEXTURE_MAG_FILTER, ToGlMagFilter(texture.sampler.mag_filter));

            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            out_gpu.texture.SetImage2D(0,
                                       formats->first,
                                       image->width,
                                       image->height,
                                       formats->second,
                                       image->is_hdr ? GL_FLOAT : GL_UNSIGNED_BYTE,
                                       image->is_hdr ? static_cast<const void *>(image->pixels_f32.data())
                                                     : static_cast<const void *>(image->pixels.data()));
            if (texture.sampler.mip_filter != core::scene::MipFilter::None)
            {
                out_gpu.texture.GenerateMipmap();
            }
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            return true;
        }

        bool UploadPrimitiveToGpu(const core::scene::MeshPrimitive &primitive,
                                  GpuSceneResourceCache::CachedPrimitiveGpu &out_gpu)
        {
            if (primitive.vertices.empty() || primitive.indices.empty())
            {
                return false;
            }

            if (!out_gpu.vao.Create())
            {
                return false;
            }
            if (!out_gpu.vertex_buffer.IsValid() || !out_gpu.index_buffer.IsValid())
            {
                return false;
            }

            out_gpu.vao.Bind();

            out_gpu.vertex_buffer.Bind();
            out_gpu.vertex_buffer.SetData(
                static_cast<GLsizeiptr>(primitive.vertices.size() * sizeof(core::scene::Vertex)),
                primitive.vertices.data(),
                GL_STATIC_DRAW);

            out_gpu.index_buffer.Bind();
            out_gpu.index_buffer.SetData(
                static_cast<GLsizeiptr>(primitive.indices.size() * sizeof(uint32_t)),
                primitive.indices.data(),
                GL_STATIC_DRAW);

            out_gpu.vao.EnableAttrib(0);
            out_gpu.vao.SetAttribPointer(
                0, 3, GL_FLOAT, false, sizeof(core::scene::Vertex), offsetof(core::scene::Vertex, position));

            out_gpu.vao.EnableAttrib(1);
            out_gpu.vao.SetAttribPointer(
                1, 3, GL_FLOAT, false, sizeof(core::scene::Vertex), offsetof(core::scene::Vertex, normal));

            out_gpu.vao.EnableAttrib(2);
            out_gpu.vao.SetAttribPointer(
                2, 2, GL_FLOAT, false, sizeof(core::scene::Vertex), offsetof(core::scene::Vertex, uv0));

            out_gpu.vao.EnableAttrib(3);
            out_gpu.vao.SetAttribPointer(
                3, 2, GL_FLOAT, false, sizeof(core::scene::Vertex), offsetof(core::scene::Vertex, uv1));

            out_gpu.vao.EnableAttrib(4);
            out_gpu.vao.SetAttribPointer(
                4, 4, GL_FLOAT, false, sizeof(core::scene::Vertex), offsetof(core::scene::Vertex, tangent));

            GLVertexArray::Unbind();
            GLBuffer::Unbind(GL_ARRAY_BUFFER);
            GLBuffer::Unbind(GL_ELEMENT_ARRAY_BUFFER);

            out_gpu.index_count = static_cast<GLsizei>(primitive.indices.size());
            return true;
        }
    } // namespace

    size_t GpuSceneResourceCache::PrimitiveCacheKeyHash::operator()(const PrimitiveCacheKey &key) const noexcept
    {
        const size_t h1 = std::hash<uint64_t>{}(key.mesh_id);
        const size_t h2 = std::hash<uint32_t>{}(key.primitive_index);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }

    bool GpuSceneResourceCache::GetOrUploadPrimitive(uint64_t mesh_id,
                                                     uint32_t primitive_index,
                                                     const core::scene::MeshPrimitive &primitive,
                                                     CachedPrimitiveGpu *&out_gpu,
                                                     bool &out_cache_miss,
                                                     bool &out_uploaded)
    {
        out_gpu = nullptr;
        out_cache_miss = false;
        out_uploaded = false;

        PrimitiveCacheKey key{};
        key.mesh_id = mesh_id;
        key.primitive_index = primitive_index;

        auto it = m_primitive_cache.find(key);
        if (it != m_primitive_cache.end())
        {
            out_gpu = &it->second;
            return true;
        }

        out_cache_miss = true;

        CachedPrimitiveGpu cached_primitive{};
        if (!UploadPrimitiveToGpu(primitive, cached_primitive))
        {
            return false;
        }

        it = m_primitive_cache.emplace(key, std::move(cached_primitive)).first;
        out_gpu = &it->second;
        out_uploaded = true;
        return true;
    }

    bool GpuSceneResourceCache::GetOrUploadTexture(const core::scene::MaterialTexture &texture,
                                                   CachedTextureGpu *&out_gpu,
                                                   bool &out_cache_miss,
                                                   bool &out_uploaded)
    {
        out_gpu = nullptr;
        out_cache_miss = false;
        out_uploaded = false;

        if (!texture.image.IsValid())
        {
            return false;
        }

        const uint64_t image_id = texture.image.Id().value;
        if (image_id == 0)
        {
            return false;
        }

        auto it = m_texture_cache.find(image_id);
        if (it != m_texture_cache.end())
        {
            out_gpu = &it->second;
            return true;
        }

        out_cache_miss = true;

        CachedTextureGpu texture_gpu{};
        if (!UploadTextureToGpu(texture, texture_gpu))
        {
            return false;
        }

        it = m_texture_cache.emplace(image_id, std::move(texture_gpu)).first;
        out_gpu = &it->second;
        out_uploaded = true;
        return true;
    }

    void GpuSceneResourceCache::Clear()
    {
        m_primitive_cache.clear();
        m_texture_cache.clear();
    }

} // namespace hybrid::renderer
