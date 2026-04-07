#include "renderer/passes/GBufferPass.h"

#include "core/Profiling.h"
#include "renderer/opengl/GLBuffer.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLTexture.h"
#include "renderer/opengl/GLVertexArray.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kInstanceSsboBinding = 1;
        constexpr GLuint kMaterialSsboBinding = 2;

        struct GpuInstanceData
        {
            glm::mat4 model{1.0f};
            glm::mat4 normal_matrix{1.0f};
            uint32_t material_index = 0;
            uint32_t entity_id = 0;
            uint32_t pad0 = 0;
            uint32_t pad1 = 0;
        };

        struct GpuMaterialData
        {
            glm::vec4 base_color_alpha{1.0f};
            glm::vec4 metallic_roughness_normal_scale_alpha_cutoff{0.0f};
            glm::uvec4 flags{0u};
            glm::uvec4 texcoord_selectors{0u};
        };

        struct PrimitiveCacheKey
        {
            uint64_t mesh_id = 0;
            uint32_t primitive_index = 0;

            bool operator==(const PrimitiveCacheKey &other) const
            {
                return mesh_id == other.mesh_id && primitive_index == other.primitive_index;
            }
        };

        struct PrimitiveCacheKeyHash
        {
            size_t operator()(const PrimitiveCacheKey &key) const noexcept
            {
                size_t h1 = std::hash<uint64_t>{}(key.mesh_id);
                size_t h2 = std::hash<uint32_t>{}(key.primitive_index);
                return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
            }
        };

        struct CachedPrimitiveGpu
        {
            GLVertexArray vao{};
            GLBuffer vertex_buffer{GL_ARRAY_BUFFER};
            GLBuffer index_buffer{GL_ELEMENT_ARRAY_BUFFER};
            GLsizei index_count = 0;
        };

        struct CachedTextureGpu
        {
            GLTexture texture{GL_TEXTURE_2D};
        };

        struct DrawItem
        {
            CachedPrimitiveGpu *gpu = nullptr;
            uint32_t instance_index = 0;
            uint64_t mesh_id = 0;
            uint32_t primitive_index = 0;
            uint32_t material_index = 0;
            GLuint base_color_texture_id = 0;
            GLuint metallic_roughness_texture_id = 0;
            GLuint normal_texture_id = 0;
        };


        template <typename Fn>
        void ForEachOpaqueAndMaskedMeshInstance(const FrameSceneData &scene, Fn &&fn)
        {
            for (const auto &instance : scene.opaque_mesh_instances)
            {
                fn(instance);
            }

            for (const auto &instance : scene.masked_mesh_instances)
            {
                fn(instance);
            }
        }

        glm::vec3 ResolvePrimitiveBaseColor(const core::scene::MeshPrimitive &primitive)
        {
            if (const auto *material = primitive.material.Get())
            {
                return glm::vec3(material->base_color_factor);
            }
            return glm::vec3(0.8f);
        }

        float ResolvePrimitiveMetallic(const core::scene::MeshPrimitive &primitive)
        {
            if (const auto *material = primitive.material.Get())
            {
                return material->metallic_factor;
            }
            return 0.0f;
        }

        float ResolvePrimitiveRoughness(const core::scene::MeshPrimitive &primitive)
        {
            if (const auto *material = primitive.material.Get())
            {
                return material->roughness_factor;
            }
            return 1.0f;
        }

        float ResolvePrimitiveBaseAlpha(const core::scene::MeshPrimitive &primitive)
        {
            if (const auto *material = primitive.material.Get())
            {
                return material->base_color_factor.a;
            }
            return 1.0f;
        }

        bool ResolvePrimitiveAlphaMasked(const core::scene::MeshPrimitive &primitive)
        {
            if (const auto *material = primitive.material.Get())
            {
                return material->alpha_mode == core::scene::AlphaMode::Mask;
            }
            return false;
        }

        float ResolvePrimitiveAlphaCutoff(const core::scene::MeshPrimitive &primitive)
        {
            if (const auto *material = primitive.material.Get())
            {
                return material->alpha_cutoff;
            }
            return 0.5f;
        }

        const core::scene::MaterialTexture *ResolvePrimitiveBaseColorTexture(const core::scene::MeshPrimitive &primitive)
        {
            const auto *material = primitive.material.Get();
            if (material == nullptr)
            {
                return nullptr;
            }
            if (!material->base_color_texture.image.IsValid())
            {
                return nullptr;
            }
            return &material->base_color_texture;
        }

        int ResolvePrimitiveBaseColorTexcoord(const core::scene::MeshPrimitive &primitive)
        {
            const auto *base_color_texture = ResolvePrimitiveBaseColorTexture(primitive);
            if (base_color_texture == nullptr)
            {
                return 0;
            }
            return base_color_texture->texcoord == 1 ? 1 : 0;
        }

        const core::scene::MaterialTexture *ResolvePrimitiveMetallicRoughnessTexture(const core::scene::MeshPrimitive &primitive)
        {
            const auto *material = primitive.material.Get();
            if (material == nullptr)
            {
                return nullptr;
            }
            if (!material->metallic_roughness_texture.image.IsValid())
            {
                return nullptr;
            }
            return &material->metallic_roughness_texture;
        }

        const core::scene::MaterialTexture *ResolvePrimitiveNormalTexture(const core::scene::MeshPrimitive &primitive)
        {
            const auto *material = primitive.material.Get();
            if (material == nullptr)
            {
                return nullptr;
            }
            if (!material->normal_texture.image.IsValid())
            {
                return nullptr;
            }
            return &material->normal_texture;
        }

        int ResolvePrimitiveMetallicRoughnessTexcoord(const core::scene::MeshPrimitive &primitive)
        {
            const auto *metallic_roughness_texture = ResolvePrimitiveMetallicRoughnessTexture(primitive);
            if (metallic_roughness_texture == nullptr)
            {
                return 0;
            }
            return metallic_roughness_texture->texcoord == 1 ? 1 : 0;
        }

        int ResolvePrimitiveNormalTexcoord(const core::scene::MeshPrimitive &primitive)
        {
            const auto *normal_texture = ResolvePrimitiveNormalTexture(primitive);
            if (normal_texture == nullptr)
            {
                return 0;
            }
            return normal_texture->texcoord == 1 ? 1 : 0;
        }

        float ResolvePrimitiveNormalScale(const core::scene::MeshPrimitive &primitive)
        {
            if (const auto *material = primitive.material.Get())
            {
                return material->normal_scale;
            }
            return 1.0f;
        }

        glm::mat4 ComputeNormalMatrix(const glm::mat4 &world_from_local)
        {
            const glm::mat3 normal_matrix_3x3 = glm::transpose(glm::inverse(glm::mat3(world_from_local)));

            glm::mat4 normal_matrix_4x4{1.0f};
            normal_matrix_4x4[0] = glm::vec4(normal_matrix_3x3[0], 0.0f);
            normal_matrix_4x4[1] = glm::vec4(normal_matrix_3x3[1], 0.0f);
            normal_matrix_4x4[2] = glm::vec4(normal_matrix_3x3[2], 0.0f);
            normal_matrix_4x4[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            return normal_matrix_4x4;
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
                                CachedTextureGpu &out_gpu)
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

        bool UploadPrimitiveToGpu(const core::scene::MeshPrimitive &primitive, CachedPrimitiveGpu &out_gpu)
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

    struct GBufferPass::Impl
    {
        std::unordered_map<PrimitiveCacheKey, CachedPrimitiveGpu, PrimitiveCacheKeyHash> primitive_cache;
        std::unordered_map<uint64_t, CachedTextureGpu> texture_cache;
        GLBuffer instance_ssbo{GL_SHADER_STORAGE_BUFFER};
        GLBuffer material_ssbo{GL_SHADER_STORAGE_BUFFER};
        size_t instance_ssbo_capacity = 0;
        size_t material_ssbo_capacity = 0;
        GLTexture white_texture{GL_TEXTURE_2D};
        GLTexture flat_normal_texture{GL_TEXTURE_2D};
        bool white_texture_initialized = false;
        bool flat_normal_texture_initialized = false;
    };

    GBufferPass::GBufferPass(GLShaderProgram *gbuffer_shader)
        : m_gbuffer_shader(gbuffer_shader),
          m_impl(std::make_unique<Impl>())
    {
    }

    GBufferPass::~GBufferPass() = default;

    const char *GBufferPass::Name() const
    {
        return "GBuffer";
    }

    bool GBufferPass::Execute(const GBufferPassInput &input, GBufferPassOutput &output)
    {
        HYBRID_PROFILE_ZONE_N("GBufferPass::Execute");
        HYBRID_PROFILE_GL_ZONE("GBufferPass");

        if (m_gbuffer_shader == nullptr ||
            m_impl == nullptr ||
            input.scene_data == nullptr ||
            input.effective_view == nullptr ||
            input.settings == nullptr ||
            input.gbuffer_framebuffer_id == 0)
        {
            return false;
        }

        const FrameSceneData &scene = *input.scene_data;
        const RenderView &effective_view = *input.effective_view;
        const RenderSettings &settings = *input.settings;
        RendererStats::GBufferStats *gbuffer_stats =
            input.renderer_stats != nullptr ? &input.renderer_stats->gbuffer : nullptr;

        {
            HYBRID_PROFILE_ZONE_N("GBufferPass::Setup");
            glBindFramebuffer(GL_FRAMEBUFFER, input.gbuffer_framebuffer_id);
            glViewport(0, 0,
                       static_cast<GLsizei>(settings.render_extent.width),
                       static_cast<GLsizei>(settings.render_extent.height));
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

            const GLfloat clear_rt0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            const GLfloat clear_rt1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            const GLuint clear_entity_id[1] = {std::numeric_limits<uint32_t>::max()};
            glClearBufferfv(GL_COLOR, 0, clear_rt0);
            glClearBufferfv(GL_COLOR, 1, clear_rt1);
            glClearBufferuiv(GL_COLOR, 2, clear_entity_id);
            glClear(GL_DEPTH_BUFFER_BIT);

            m_gbuffer_shader->Use();
            m_gbuffer_shader->SetUniformMat4("u_view", effective_view.view);
            m_gbuffer_shader->SetUniformMat4("u_projection", effective_view.projection);
            m_gbuffer_shader->SetUniform1i("u_base_color_texture", 0);
            m_gbuffer_shader->SetUniform1i("u_metallic_roughness_texture", 1);
            m_gbuffer_shader->SetUniform1i("u_normal_texture", 2);

            if (gbuffer_stats != nullptr)
            {
                gbuffer_stats->uniform_updates += 5;
            }
        }

        if (!m_impl->white_texture_initialized)
        {
            HYBRID_PROFILE_ZONE_N("GBufferPass::InitWhiteTexture");
            constexpr uint8_t kWhiteRgba[4] = {255, 255, 255, 255};
            m_impl->white_texture.Bind();
            m_impl->white_texture.SetParameter(GL_TEXTURE_WRAP_S, GL_REPEAT);
            m_impl->white_texture.SetParameter(GL_TEXTURE_WRAP_T, GL_REPEAT);
            m_impl->white_texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            m_impl->white_texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            m_impl->white_texture.SetImage2D(0, GL_RGBA8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, kWhiteRgba);
            m_impl->white_texture_initialized = true;
        }

        if (!m_impl->flat_normal_texture_initialized)
        {
            HYBRID_PROFILE_ZONE_N("GBufferPass::InitFlatNormalTexture");
            // Tangent-space (0,0,1) encoded to [0,1] range.
            constexpr uint8_t kFlatNormalRgba[4] = {128, 128, 255, 255};
            m_impl->flat_normal_texture.Bind();
            m_impl->flat_normal_texture.SetParameter(GL_TEXTURE_WRAP_S, GL_REPEAT);
            m_impl->flat_normal_texture.SetParameter(GL_TEXTURE_WRAP_T, GL_REPEAT);
            m_impl->flat_normal_texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            m_impl->flat_normal_texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            m_impl->flat_normal_texture.SetImage2D(0, GL_RGBA8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, kFlatNormalRgba);
            m_impl->flat_normal_texture_initialized = true;
        }

        std::vector<DrawItem> draw_items{};
        std::vector<GpuInstanceData> instance_gpu_data{};
        std::vector<GpuMaterialData> material_gpu_data{};
        struct CachedMaterialFrameRecord
        {
            uint32_t material_index = 0;
            GLuint base_color_texture_id = 0;
            GLuint metallic_roughness_texture_id = 0;
            GLuint normal_texture_id = 0;
        };
        std::unordered_map<uint64_t, CachedMaterialFrameRecord> material_frame_cache{};

        auto ensure_ssbo_storage = [](GLBuffer &ssbo,
                                      size_t &capacity_elements,
                                      size_t required_elements,
                                      const size_t element_size) -> bool
        {
            if (!ssbo.IsValid() && !ssbo.Create(GL_SHADER_STORAGE_BUFFER))
            {
                return false;
            }

            if (required_elements <= capacity_elements)
            {
                return true;
            }

            size_t new_capacity = capacity_elements > 0 ? capacity_elements : 1;
            while (new_capacity < required_elements)
            {
                if (new_capacity > std::numeric_limits<size_t>::max() / 2)
                {
                    new_capacity = required_elements;
                    break;
                }
                new_capacity *= 2;
            }

            ssbo.Bind();
            ssbo.SetData(static_cast<GLsizeiptr>(new_capacity * element_size), nullptr, GL_DYNAMIC_DRAW);
            capacity_elements = new_capacity;
            return true;
        };

        enum class TextureUploadZone
        {
            BaseColor,
            MetallicRoughness,
            Normal
        };

        auto resolve_texture_gpu_id = [&](const core::scene::MaterialTexture *texture,
                                          GLuint fallback_texture_id,
                                          TextureUploadZone upload_zone,
                                          bool &out_has_texture) -> GLuint
        {
            out_has_texture = false;
            if (texture == nullptr)
            {
                return fallback_texture_id;
            }

            const uint64_t image_id = texture->image.Id().value;
            if (image_id == 0)
            {
                return fallback_texture_id;
            }

            auto cached_texture_it = m_impl->texture_cache.find(image_id);
            if (cached_texture_it == m_impl->texture_cache.end())
            {
                if (gbuffer_stats != nullptr)
                {
                    gbuffer_stats->texture_cache_misses++;
                }

                if (upload_zone == TextureUploadZone::BaseColor)
                {
                    HYBRID_PROFILE_ZONE_N("GBufferPass::UploadBaseColorTextureCacheMiss");
                }
                else if (upload_zone == TextureUploadZone::MetallicRoughness)
                {
                    HYBRID_PROFILE_ZONE_N("GBufferPass::UploadMetallicRoughnessTextureCacheMiss");
                }
                else
                {
                    HYBRID_PROFILE_ZONE_N("GBufferPass::UploadNormalTextureCacheMiss");
                }
                CachedTextureGpu texture_gpu{};
                if (UploadTextureToGpu(*texture, texture_gpu))
                {
                    if (gbuffer_stats != nullptr)
                    {
                        gbuffer_stats->texture_uploads++;
                    }
                    cached_texture_it = m_impl->texture_cache.emplace(image_id, std::move(texture_gpu)).first;
                }
            }

            if (cached_texture_it == m_impl->texture_cache.end())
            {
                return fallback_texture_id;
            }

            out_has_texture = true;
            return cached_texture_it->second.texture.Id();
        };

        auto append_draw_items_for_instance = [&](const RenderMeshInstance &instance)
        {
            const core::scene::MeshAsset *mesh = instance.mesh.Get();
            if (mesh == nullptr)
            {
                return;
            }
            const glm::mat4 instance_normal_matrix = ComputeNormalMatrix(instance.world_from_local);

            for (size_t primitive_index = 0; primitive_index < mesh->primitives.size(); ++primitive_index)
            {
                const core::scene::MeshPrimitive &primitive = mesh->primitives[primitive_index];

                PrimitiveCacheKey key{};
                key.mesh_id = instance.mesh.Id().value;
                key.primitive_index = static_cast<uint32_t>(primitive_index);

                auto gpu_it = m_impl->primitive_cache.find(key);
                if (gpu_it == m_impl->primitive_cache.end())
                {
                    if (gbuffer_stats != nullptr)
                    {
                        gbuffer_stats->primitive_cache_misses++;
                    }

                    HYBRID_PROFILE_ZONE_N("GBufferPass::UploadPrimitiveCacheMiss");
                    CachedPrimitiveGpu cached_primitive{};
                    if (!UploadPrimitiveToGpu(primitive, cached_primitive))
                    {
                        continue;
                    }
                    if (gbuffer_stats != nullptr)
                    {
                        gbuffer_stats->primitive_uploads++;
                    }
                    gpu_it = m_impl->primitive_cache.emplace(key, std::move(cached_primitive)).first;
                }

                CachedPrimitiveGpu &gpu = gpu_it->second;
                if (gpu.index_count == 0)
                {
                    continue;
                }

                const uint64_t material_asset_id = primitive.material.Id().value;
                CachedMaterialFrameRecord material_record{};

                const auto cached_material_it = material_frame_cache.find(material_asset_id);
                if (cached_material_it != material_frame_cache.end())
                {
                    material_record = cached_material_it->second;
                }
                else
                {
                    bool has_base_color_texture = false;
                    const GLuint base_color_texture_id = resolve_texture_gpu_id(ResolvePrimitiveBaseColorTexture(primitive),
                                                                                m_impl->white_texture.Id(),
                                                                                TextureUploadZone::BaseColor,
                                                                                has_base_color_texture);

                    bool has_metallic_roughness_texture = false;
                    const GLuint metallic_roughness_texture_id = resolve_texture_gpu_id(ResolvePrimitiveMetallicRoughnessTexture(primitive),
                                                                                        m_impl->white_texture.Id(),
                                                                                        TextureUploadZone::MetallicRoughness,
                                                                                        has_metallic_roughness_texture);

                    bool has_normal_texture = false;
                    const GLuint normal_texture_id = resolve_texture_gpu_id(ResolvePrimitiveNormalTexture(primitive),
                                                                            m_impl->flat_normal_texture.Id(),
                                                                            TextureUploadZone::Normal,
                                                                            has_normal_texture);

                    GpuMaterialData material_data{};
                    material_data.base_color_alpha = glm::vec4(ResolvePrimitiveBaseColor(primitive), ResolvePrimitiveBaseAlpha(primitive));
                    material_data.metallic_roughness_normal_scale_alpha_cutoff = glm::vec4(ResolvePrimitiveMetallic(primitive),
                                                                                            ResolvePrimitiveRoughness(primitive),
                                                                                            ResolvePrimitiveNormalScale(primitive),
                                                                                            ResolvePrimitiveAlphaCutoff(primitive));
                    material_data.flags = glm::uvec4(has_base_color_texture ? 1u : 0u,
                                                     has_metallic_roughness_texture ? 1u : 0u,
                                                     has_normal_texture ? 1u : 0u,
                                                     ResolvePrimitiveAlphaMasked(primitive) ? 1u : 0u);
                    material_data.texcoord_selectors = glm::uvec4(ResolvePrimitiveBaseColorTexcoord(primitive) == 1 ? 1u : 0u,
                                                                  ResolvePrimitiveMetallicRoughnessTexcoord(primitive) == 1 ? 1u : 0u,
                                                                  ResolvePrimitiveNormalTexcoord(primitive) == 1 ? 1u : 0u,
                                                                  0u);

                    material_record.material_index = static_cast<uint32_t>(material_gpu_data.size());
                    material_record.base_color_texture_id = base_color_texture_id;
                    material_record.metallic_roughness_texture_id = metallic_roughness_texture_id;
                    material_record.normal_texture_id = normal_texture_id;
                    material_gpu_data.push_back(material_data);
                    material_frame_cache.emplace(material_asset_id, material_record);
                }

                GpuInstanceData instance_data{};
                instance_data.model = instance.world_from_local;
                instance_data.normal_matrix = instance_normal_matrix;
                instance_data.material_index = material_record.material_index;
                instance_data.entity_id = static_cast<uint32_t>(instance.instance_id);
                const uint32_t instance_index = static_cast<uint32_t>(instance_gpu_data.size());
                instance_gpu_data.push_back(instance_data);

                DrawItem draw_item{};
                draw_item.gpu = &gpu;
                draw_item.instance_index = instance_index;
                draw_item.mesh_id = key.mesh_id;
                draw_item.primitive_index = key.primitive_index;
                draw_item.material_index = material_record.material_index;
                draw_item.base_color_texture_id = material_record.base_color_texture_id;
                draw_item.metallic_roughness_texture_id = material_record.metallic_roughness_texture_id;
                draw_item.normal_texture_id = material_record.normal_texture_id;
                draw_items.push_back(draw_item);
            }
        };

        {
            HYBRID_PROFILE_ZONE_N("GBufferPass::DrawOpaqueMasked");
            ForEachOpaqueAndMaskedMeshInstance(scene, append_draw_items_for_instance);

            if (!draw_items.empty())
            {
                std::stable_sort(draw_items.begin(), draw_items.end(), [](const DrawItem &a, const DrawItem &b)
                                 {
                                     if (a.material_index != b.material_index)
                                     {
                                         return a.material_index < b.material_index;
                                     }
                                     if (a.mesh_id != b.mesh_id)
                                     {
                                         return a.mesh_id < b.mesh_id;
                                     }
                                     return a.primitive_index < b.primitive_index;
                                 });

                if (!ensure_ssbo_storage(m_impl->instance_ssbo, m_impl->instance_ssbo_capacity, instance_gpu_data.size(), sizeof(GpuInstanceData)) ||
                    !ensure_ssbo_storage(m_impl->material_ssbo, m_impl->material_ssbo_capacity, material_gpu_data.size(), sizeof(GpuMaterialData)))
                {
                    return false;
                }

                m_impl->instance_ssbo.Bind();
                m_impl->instance_ssbo.SetSubData(0,
                                                 static_cast<GLsizeiptr>(instance_gpu_data.size() * sizeof(GpuInstanceData)),
                                                 instance_gpu_data.data());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kInstanceSsboBinding, m_impl->instance_ssbo.Id());

                m_impl->material_ssbo.Bind();
                m_impl->material_ssbo.SetSubData(0,
                                                 static_cast<GLsizeiptr>(material_gpu_data.size() * sizeof(GpuMaterialData)),
                                                 material_gpu_data.data());
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, kMaterialSsboBinding, m_impl->material_ssbo.Id());

                GLuint last_base_color_texture = std::numeric_limits<GLuint>::max();
                GLuint last_metallic_roughness_texture = std::numeric_limits<GLuint>::max();
                GLuint last_normal_texture = std::numeric_limits<GLuint>::max();
                GLuint last_vao = std::numeric_limits<GLuint>::max();

                for (const DrawItem &draw_item : draw_items)
                {
                    if (draw_item.base_color_texture_id != last_base_color_texture)
                    {
                        glActiveTexture(GL_TEXTURE0);
                        glBindTexture(GL_TEXTURE_2D, draw_item.base_color_texture_id);
                        last_base_color_texture = draw_item.base_color_texture_id;
                        if (gbuffer_stats != nullptr)
                        {
                            gbuffer_stats->texture_binds++;
                        }
                    }
                    if (draw_item.metallic_roughness_texture_id != last_metallic_roughness_texture)
                    {
                        glActiveTexture(GL_TEXTURE1);
                        glBindTexture(GL_TEXTURE_2D, draw_item.metallic_roughness_texture_id);
                        last_metallic_roughness_texture = draw_item.metallic_roughness_texture_id;
                        if (gbuffer_stats != nullptr)
                        {
                            gbuffer_stats->texture_binds++;
                        }
                    }
                    if (draw_item.normal_texture_id != last_normal_texture)
                    {
                        glActiveTexture(GL_TEXTURE2);
                        glBindTexture(GL_TEXTURE_2D, draw_item.normal_texture_id);
                        last_normal_texture = draw_item.normal_texture_id;
                        if (gbuffer_stats != nullptr)
                        {
                            gbuffer_stats->texture_binds++;
                        }
                    }

                    const GLuint vao_id = draw_item.gpu->vao.Id();
                    if (vao_id != last_vao)
                    {
                        draw_item.gpu->vao.Bind();
                        last_vao = vao_id;
                    }
                    glDrawElementsInstancedBaseInstance(GL_TRIANGLES,
                                                        draw_item.gpu->index_count,
                                                        GL_UNSIGNED_INT,
                                                        nullptr,
                                                        1,
                                                        draw_item.instance_index);
                    if (gbuffer_stats != nullptr)
                    {
                        gbuffer_stats->draw_calls++;
                    }
                }
            }
        }

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        output.gbuffer_rt0 = input.gbuffer_rt0;
        output.gbuffer_rt1 = input.gbuffer_rt1;
        output.gbuffer_entity_id = input.gbuffer_entity_id;
        output.depth = input.gbuffer_depth;
        return true;
    }

} // namespace hybrid::renderer
