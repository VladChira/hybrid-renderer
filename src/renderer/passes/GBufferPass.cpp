#include "renderer/passes/GBufferPass.h"

#include "core/Profiling.h"
#include "renderer/opengl/GLBuffer.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLTexture.h"
#include "renderer/opengl/GLVertexArray.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

namespace hybrid::renderer
{

    namespace
    {
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

        struct ResolvedMaterialTexture
        {
            const core::scene::MaterialTexture *texture = nullptr;
            uint64_t image_id = 0;
            bool has_texture = false;
            int texcoord = 0;
        };

        struct ResolvedMaterial
        {
            glm::vec3 base_color{0.8f};
            float base_alpha = 1.0f;
            float metallic = 0.0f;
            float roughness = 1.0f;
            bool alpha_masked = false;
            float alpha_cutoff = 0.5f;
            float normal_scale = 1.0f;
            ResolvedMaterialTexture base_color_texture{};
            ResolvedMaterialTexture metallic_roughness_texture{};
            ResolvedMaterialTexture normal_texture{};
        };

        ResolvedMaterialTexture ResolveMaterialTexture(const core::scene::MaterialTexture &texture)
        {
            ResolvedMaterialTexture resolved{};
            resolved.texcoord = texture.texcoord == 1 ? 1 : 0;

            if (!texture.image.IsValid())
            {
                return resolved;
            }

            const uint64_t image_id = texture.image.Id().value;
            if (image_id == 0)
            {
                return resolved;
            }

            resolved.texture = &texture;
            resolved.image_id = image_id;
            resolved.has_texture = true;
            return resolved;
        }

        ResolvedMaterial ResolvePrimitiveMaterial(const core::scene::MeshPrimitive &primitive)
        {
            ResolvedMaterial resolved{};
            const auto *material = primitive.material.Get();
            if (material == nullptr)
            {
                return resolved;
            }

            resolved.base_color = glm::vec3(material->base_color_factor);
            resolved.base_alpha = material->base_color_factor.a;
            resolved.metallic = material->metallic_factor;
            resolved.roughness = material->roughness_factor;
            resolved.alpha_masked = material->alpha_mode == core::scene::AlphaMode::Mask;
            resolved.alpha_cutoff = material->alpha_cutoff;
            resolved.normal_scale = material->normal_scale;
            resolved.base_color_texture = ResolveMaterialTexture(material->base_color_texture);
            resolved.metallic_roughness_texture = ResolveMaterialTexture(material->metallic_roughness_texture);
            resolved.normal_texture = ResolveMaterialTexture(material->normal_texture);
            return resolved;
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
        std::unordered_map<uint64_t, ResolvedMaterial> material_cache;

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

        auto draw_instance = [&](const RenderMeshInstance &instance)
        {
            const core::scene::MeshAsset *mesh = instance.mesh.Get();
            if (mesh == nullptr)
            {
                return;
            }

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
                auto material_it = material_cache.find(material_asset_id);
                if (material_it == material_cache.end())
                {
                    material_it = material_cache.emplace(material_asset_id, ResolvePrimitiveMaterial(primitive)).first;
                }
                const ResolvedMaterial &resolved_material = material_it->second;

                m_gbuffer_shader->SetUniformMat4("u_model", instance.world_from_local);
                m_gbuffer_shader->SetUniformVec3("u_base_color", resolved_material.base_color);
                m_gbuffer_shader->SetUniform1f("u_base_alpha", resolved_material.base_alpha);
                m_gbuffer_shader->SetUniform1f("u_metallic", resolved_material.metallic);
                m_gbuffer_shader->SetUniform1f("u_roughness", resolved_material.roughness);
                m_gbuffer_shader->SetUniform1i("u_alpha_masked", resolved_material.alpha_masked ? 1 : 0);
                m_gbuffer_shader->SetUniform1f("u_alpha_cutoff", resolved_material.alpha_cutoff);
                m_gbuffer_shader->SetUniform1i("u_base_color_texcoord", resolved_material.base_color_texture.texcoord);
                m_gbuffer_shader->SetUniform1i("u_metallic_roughness_texcoord", resolved_material.metallic_roughness_texture.texcoord);
                m_gbuffer_shader->SetUniform1i("u_normal_texcoord", resolved_material.normal_texture.texcoord);
                m_gbuffer_shader->SetUniform1f("u_normal_scale", resolved_material.normal_scale);
                m_gbuffer_shader->SetUniform1ui("u_instance_id", static_cast<uint32_t>(instance.instance_id));
                if (gbuffer_stats != nullptr)
                {
                    // Logical count of per-draw uniform writes we issue.
                    gbuffer_stats->uniform_updates += 12;
                }

                bool has_base_color_texture = false;
                if (resolved_material.base_color_texture.has_texture)
                {
                    auto cached_texture_it = m_impl->texture_cache.find(resolved_material.base_color_texture.image_id);
                    if (cached_texture_it == m_impl->texture_cache.end())
                    {
                        if (gbuffer_stats != nullptr)
                        {
                            gbuffer_stats->texture_cache_misses++;
                        }

                        HYBRID_PROFILE_ZONE_N("GBufferPass::UploadBaseColorTextureCacheMiss");
                        CachedTextureGpu texture_gpu{};
                        if (UploadTextureToGpu(*resolved_material.base_color_texture.texture, texture_gpu))
                        {
                            if (gbuffer_stats != nullptr)
                            {
                                gbuffer_stats->texture_uploads++;
                            }
                            cached_texture_it = m_impl->texture_cache.emplace(
                                                                      resolved_material.base_color_texture.image_id,
                                                                      std::move(texture_gpu))
                                                  .first;
                        }
                    }

                    if (cached_texture_it != m_impl->texture_cache.end())
                    {
                        cached_texture_it->second.texture.BindToUnit(0);
                        has_base_color_texture = true;
                        if (gbuffer_stats != nullptr)
                        {
                            gbuffer_stats->texture_binds++;
                        }
                    }
                }

                if (!has_base_color_texture)
                {
                    m_impl->white_texture.BindToUnit(0);
                    if (gbuffer_stats != nullptr)
                    {
                        gbuffer_stats->texture_binds++;
                    }
                }
                m_gbuffer_shader->SetUniform1i("u_has_base_color_texture", has_base_color_texture ? 1 : 0);
                if (gbuffer_stats != nullptr)
                {
                    gbuffer_stats->uniform_updates++;
                }

                bool has_metallic_roughness_texture = false;
                if (resolved_material.metallic_roughness_texture.has_texture)
                {
                    auto cached_texture_it = m_impl->texture_cache.find(resolved_material.metallic_roughness_texture.image_id);
                    if (cached_texture_it == m_impl->texture_cache.end())
                    {
                        if (gbuffer_stats != nullptr)
                        {
                            gbuffer_stats->texture_cache_misses++;
                        }

                        HYBRID_PROFILE_ZONE_N("GBufferPass::UploadMetallicRoughnessTextureCacheMiss");
                        CachedTextureGpu texture_gpu{};
                        if (UploadTextureToGpu(*resolved_material.metallic_roughness_texture.texture, texture_gpu))
                        {
                            if (gbuffer_stats != nullptr)
                            {
                                gbuffer_stats->texture_uploads++;
                            }
                            cached_texture_it = m_impl->texture_cache.emplace(
                                                                      resolved_material.metallic_roughness_texture.image_id,
                                                                      std::move(texture_gpu))
                                                  .first;
                        }
                    }

                    if (cached_texture_it != m_impl->texture_cache.end())
                    {
                        cached_texture_it->second.texture.BindToUnit(1);
                        has_metallic_roughness_texture = true;
                        if (gbuffer_stats != nullptr)
                        {
                            gbuffer_stats->texture_binds++;
                        }
                    }
                }

                if (!has_metallic_roughness_texture)
                {
                    m_impl->white_texture.BindToUnit(1);
                    if (gbuffer_stats != nullptr)
                    {
                        gbuffer_stats->texture_binds++;
                    }
                }
                m_gbuffer_shader->SetUniform1i("u_has_metallic_roughness_texture", has_metallic_roughness_texture ? 1 : 0);
                if (gbuffer_stats != nullptr)
                {
                    gbuffer_stats->uniform_updates++;
                }

                bool has_normal_texture = false;
                if (resolved_material.normal_texture.has_texture)
                {
                    auto cached_texture_it = m_impl->texture_cache.find(resolved_material.normal_texture.image_id);
                    if (cached_texture_it == m_impl->texture_cache.end())
                    {
                        if (gbuffer_stats != nullptr)
                        {
                            gbuffer_stats->texture_cache_misses++;
                        }

                        HYBRID_PROFILE_ZONE_N("GBufferPass::UploadNormalTextureCacheMiss");
                        CachedTextureGpu texture_gpu{};
                        if (UploadTextureToGpu(*resolved_material.normal_texture.texture, texture_gpu))
                        {
                            if (gbuffer_stats != nullptr)
                            {
                                gbuffer_stats->texture_uploads++;
                            }
                            cached_texture_it = m_impl->texture_cache.emplace(
                                                                      resolved_material.normal_texture.image_id,
                                                                      std::move(texture_gpu))
                                                  .first;
                        }
                    }

                    if (cached_texture_it != m_impl->texture_cache.end())
                    {
                        cached_texture_it->second.texture.BindToUnit(2);
                        has_normal_texture = true;
                        if (gbuffer_stats != nullptr)
                        {
                            gbuffer_stats->texture_binds++;
                        }
                    }
                }

                if (!has_normal_texture)
                {
                    m_impl->flat_normal_texture.BindToUnit(2);
                    if (gbuffer_stats != nullptr)
                    {
                        gbuffer_stats->texture_binds++;
                    }
                }
                m_gbuffer_shader->SetUniform1i("u_has_normal_texture", has_normal_texture ? 1 : 0);
                if (gbuffer_stats != nullptr)
                {
                    gbuffer_stats->uniform_updates++;
                }

                gpu.vao.Bind();
                glDrawElements(GL_TRIANGLES, gpu.index_count, GL_UNSIGNED_INT, nullptr);
                if (gbuffer_stats != nullptr)
                {
                    gbuffer_stats->draw_calls++;
                }
            }
        };

        {
            HYBRID_PROFILE_ZONE_N("GBufferPass::DrawOpaqueMasked");
            ForEachOpaqueAndMaskedMeshInstance(scene, draw_instance);
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
