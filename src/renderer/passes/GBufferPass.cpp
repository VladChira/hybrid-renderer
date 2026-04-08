#include "renderer/passes/GBufferPass.h"

#include "core/Profiling.h"
#include "renderer/GpuSceneResourceCache.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLTexture.h"
#include "renderer/opengl/GLVertexArray.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace hybrid::renderer
{

    namespace
    {
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
    } // namespace

    struct GBufferPass::Impl
    {
        GLTexture white_texture{GL_TEXTURE_2D};
        GLTexture flat_normal_texture{GL_TEXTURE_2D};
        bool white_texture_initialized = false;
        bool flat_normal_texture_initialized = false;
    };

    GBufferPass::GBufferPass(GLShaderProgram *gbuffer_shader, GpuSceneResourceCache *gpu_resource_cache)
        : m_gbuffer_shader(gbuffer_shader),
          m_gpu_resource_cache(gpu_resource_cache),
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
            m_gpu_resource_cache == nullptr ||
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

                GpuSceneResourceCache::CachedPrimitiveGpu *gpu = nullptr;
                bool primitive_cache_miss = false;
                bool primitive_uploaded = false;
                const bool has_gpu_primitive =
                    m_gpu_resource_cache->GetOrUploadPrimitive(instance.mesh.Id().value,
                                                               static_cast<uint32_t>(primitive_index),
                                                               primitive,
                                                               gpu,
                                                               primitive_cache_miss,
                                                               primitive_uploaded);
                if (gbuffer_stats != nullptr && primitive_cache_miss)
                {
                    gbuffer_stats->primitive_cache_misses++;
                    if (primitive_uploaded)
                    {
                        gbuffer_stats->primitive_uploads++;
                    }
                }

                if (!has_gpu_primitive || gpu == nullptr || gpu->index_count == 0)
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
                    GpuSceneResourceCache::CachedTextureGpu *cached_texture = nullptr;
                    bool texture_cache_miss = false;
                    bool texture_uploaded = false;
                    const bool has_gpu_texture =
                        m_gpu_resource_cache->GetOrUploadTexture(*resolved_material.base_color_texture.texture,
                                                                 cached_texture,
                                                                 texture_cache_miss,
                                                                 texture_uploaded);
                    if (gbuffer_stats != nullptr && texture_cache_miss)
                    {
                        gbuffer_stats->texture_cache_misses++;
                        if (texture_uploaded)
                        {
                            gbuffer_stats->texture_uploads++;
                        }
                    }

                    if (has_gpu_texture && cached_texture != nullptr)
                    {
                        cached_texture->texture.BindToUnit(0);
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
                    GpuSceneResourceCache::CachedTextureGpu *cached_texture = nullptr;
                    bool texture_cache_miss = false;
                    bool texture_uploaded = false;
                    const bool has_gpu_texture =
                        m_gpu_resource_cache->GetOrUploadTexture(*resolved_material.metallic_roughness_texture.texture,
                                                                 cached_texture,
                                                                 texture_cache_miss,
                                                                 texture_uploaded);
                    if (gbuffer_stats != nullptr && texture_cache_miss)
                    {
                        gbuffer_stats->texture_cache_misses++;
                        if (texture_uploaded)
                        {
                            gbuffer_stats->texture_uploads++;
                        }
                    }

                    if (has_gpu_texture && cached_texture != nullptr)
                    {
                        cached_texture->texture.BindToUnit(1);
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
                    GpuSceneResourceCache::CachedTextureGpu *cached_texture = nullptr;
                    bool texture_cache_miss = false;
                    bool texture_uploaded = false;
                    const bool has_gpu_texture =
                        m_gpu_resource_cache->GetOrUploadTexture(*resolved_material.normal_texture.texture,
                                                                 cached_texture,
                                                                 texture_cache_miss,
                                                                 texture_uploaded);
                    if (gbuffer_stats != nullptr && texture_cache_miss)
                    {
                        gbuffer_stats->texture_cache_misses++;
                        if (texture_uploaded)
                        {
                            gbuffer_stats->texture_uploads++;
                        }
                    }

                    if (has_gpu_texture && cached_texture != nullptr)
                    {
                        cached_texture->texture.BindToUnit(2);
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

                gpu->vao.Bind();
                glDrawElements(GL_TRIANGLES, gpu->index_count, GL_UNSIGNED_INT, nullptr);
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
