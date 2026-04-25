#include "renderer/passes/HdriPrecomputePass.h"

#include "assets/ImageAsset.h"
#include "core/Log.h"
#include "core/Profiling.h"
#include "renderer/opengl/GLFramebuffer.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLTexture.h"
#include "renderer/opengl/GLVertexArray.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

namespace hybrid::renderer
{
    namespace
    {
        struct HdriBakeSettings
        {
            uint32_t env_cubemap_size = 512;
            uint32_t irradiance_cubemap_size = 32;
            uint32_t prefilter_cubemap_size = 128;
            uint32_t prefilter_mip_levels = 5;
            uint32_t brdf_lut_size = 512;

            bool operator==(const HdriBakeSettings &other) const noexcept
            {
                return env_cubemap_size == other.env_cubemap_size &&
                       irradiance_cubemap_size == other.irradiance_cubemap_size &&
                       prefilter_cubemap_size == other.prefilter_cubemap_size &&
                       prefilter_mip_levels == other.prefilter_mip_levels &&
                       brdf_lut_size == other.brdf_lut_size;
            }
        };

        uint32_t MaxMipLevelsForSize(uint32_t size)
        {
            uint32_t levels = 0;
            while (size > 0)
            {
                ++levels;
                size >>= 1u;
            }
            return std::max(levels, 1u);
        }

        HdriBakeSettings SanitizeHdriBakeSettings(const RenderSettings &settings)
        {
            HdriBakeSettings result{};
            result.env_cubemap_size = std::clamp(settings.hdri_env_cubemap_size, 16u, 4096u);
            result.irradiance_cubemap_size = std::clamp(settings.hdri_irradiance_cubemap_size, 4u, 1024u);
            result.prefilter_cubemap_size = std::clamp(settings.hdri_prefilter_cubemap_size, 16u, 4096u);
            result.brdf_lut_size = std::clamp(settings.hdri_brdf_lut_size, 16u, 4096u);

            const uint32_t max_prefilter_mips = MaxMipLevelsForSize(result.prefilter_cubemap_size);
            result.prefilter_mip_levels = std::clamp(settings.hdri_prefilter_mip_levels, 1u, max_prefilter_mips);
            return result;
        }

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
            GLTexture environment_cubemap{};
            GLTexture convoluted_cubemap{};
            GLTexture prefiltered_cubemap{};
            GLTexture brdf_lut{};
        };

        struct SourceTextureGpu
        {
            GLTexture texture{};
        };
    } // namespace

    struct HdriPrecomputePass::Impl
    {
        std::unordered_map<IBLCacheKey, IBLCached, IBLCacheKeyHash> ibl_cache{};
        std::unordered_map<uint64_t, SourceTextureGpu> source_texture_cache{};
        GLFramebuffer capture_fbo{};
        GLVertexArray cube_vao{};
        HdriBakeSettings last_bake_settings{};
        bool has_last_bake_settings = false;
    };

    HdriPrecomputePass::HdriPrecomputePass(GLShaderProgram *equirect_to_cubemap_shader,
                                           GLShaderProgram *convolute_shader,
                                           GLShaderProgram *prefilter_shader,
                                           GLShaderProgram *brdf_lut_shader)
        : m_equirect_to_cubemap_shader(equirect_to_cubemap_shader),
          m_convolute_shader(convolute_shader),
          m_prefilter_shader(prefilter_shader),
          m_brdf_lut_shader(brdf_lut_shader),
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
        HYBRID_PROFILE_ZONE_N("HdriPrecomputePass::Execute");
        HYBRID_PROFILE_GL_ZONE("HdriPrecomputePass");

        if (m_impl == nullptr ||
            m_equirect_to_cubemap_shader == nullptr ||
            m_convolute_shader == nullptr ||
            m_prefilter_shader == nullptr ||
            m_brdf_lut_shader == nullptr ||
            input.scene_data == nullptr ||
            input.settings == nullptr)
        {
            return false;
        }

        output = {};
        const HdriBakeSettings bake_settings = SanitizeHdriBakeSettings(*input.settings);
        if (!m_impl->has_last_bake_settings || !(m_impl->last_bake_settings == bake_settings))
        {
            m_impl->ibl_cache.clear();
            m_impl->last_bake_settings = bake_settings;
            m_impl->has_last_bake_settings = true;
            LOG_INFO("[HdriPrecomputePass] Cleared IBL cache after HDRI bake settings change.");
        }

        const RenderHdriLight *selected_light = nullptr;
        for (const RenderHdriLight &hdri_light : input.scene_data->hdri_lights)
        {
            if (hdri_light.texture.IsValid())
            {
                selected_light = &hdri_light;
                break;
            }
        }

        if (selected_light == nullptr)
        {
            return true;
        }

        const assets::ImageAsset *source_image = selected_light->texture.Get();
        if (source_image == nullptr || !source_image->IsValid())
        {
            LOG_WARN("[HdriPrecomputePass] HDRI source image is invalid for light {}", selected_light->instance_id);
            return true;
        }

        const uint64_t source_texture_id = selected_light->texture.Id().value;
        if (source_texture_id == 0)
        {
            LOG_WARN("[HdriPrecomputePass] HDRI source texture id is zero for light {}", selected_light->instance_id);
            return true;
        }

        auto source_it = m_impl->source_texture_cache.find(source_texture_id);
        if (source_it == m_impl->source_texture_cache.end())
        {
            SourceTextureGpu source_gpu{};
            if (!source_gpu.texture.Create(GL_TEXTURE_2D))
            {
                LOG_ERROR("[HdriPrecomputePass] Failed to create source HDRI GL texture");
                return true;
            }
            source_gpu.texture.Bind();
            source_gpu.texture.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            source_gpu.texture.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            source_gpu.texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            source_gpu.texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            GLenum pixel_format = GL_RGB;
            GLint internal_format = GL_RGB8;
            switch (source_image->channels)
            {
            case 1:
                pixel_format = GL_RED;
                internal_format = source_image->is_hdr ? GL_R16F : GL_R8;
                break;
            case 2:
                pixel_format = GL_RG;
                internal_format = source_image->is_hdr ? GL_RG16F : GL_RG8;
                break;
            case 3:
                pixel_format = GL_RGB;
                internal_format = source_image->is_hdr ? GL_RGB16F : GL_RGB8;
                break;
            case 4:
                pixel_format = GL_RGBA;
                internal_format = source_image->is_hdr ? GL_RGBA16F : GL_RGBA8;
                break;
            default:
                return true;
            }

            source_gpu.texture.SetImage2D(0,
                                          internal_format,
                                          static_cast<GLsizei>(source_image->width),
                                          static_cast<GLsizei>(source_image->height),
                                          pixel_format,
                                          source_image->is_hdr ? GL_FLOAT : GL_UNSIGNED_BYTE,
                                          source_image->is_hdr ? static_cast<const void *>(source_image->pixels_f32.data())
                                                               : static_cast<const void *>(source_image->pixels.data()));

            source_it = m_impl->source_texture_cache.emplace(source_texture_id, std::move(source_gpu)).first;
        }

        IBLCacheKey cache_key{};
        cache_key.light_id = selected_light->instance_id;
        cache_key.hdri_texture_id = source_texture_id;

        auto cache_it = m_impl->ibl_cache.find(cache_key);
        if (cache_it == m_impl->ibl_cache.end())
        {
            cache_it = m_impl->ibl_cache.emplace(cache_key, IBLCached{}).first;
        }

        IBLCached &cached = cache_it->second;
        if (cached.state == IBLBakeState::Ready &&
            cached.environment_cubemap.IsValid() &&
            cached.convoluted_cubemap.IsValid() &&
            cached.prefiltered_cubemap.IsValid() &&
            cached.brdf_lut.IsValid())
        {
            output.has_skybox = true;
            output.skybox_cubemap = cached.environment_cubemap.Id();
            output.convoluted_cubemap = cached.convoluted_cubemap.Id();
            output.prefiltered_cubemap = cached.prefiltered_cubemap.Id();
            output.brdf_lut = cached.brdf_lut.Id();
            output.skybox_intensity = selected_light->intensity;
            output.skybox_yaw_radians = selected_light->yaw_radians;
            return true;
        }

        LOG_INFO("[HdriPrecomputePass] Starting HDRI precompute for light {} (texture id {}).",
                 selected_light->instance_id,
                 source_texture_id);

        const uint32_t env_cubemap_mip_levels = MaxMipLevelsForSize(bake_settings.env_cubemap_size);
        if (!cached.environment_cubemap.IsValid())
        {
            if (!cached.environment_cubemap.Create(GL_TEXTURE_CUBE_MAP))
            {
                cached.state = IBLBakeState::Failed;
                return true;
            }

            cached.environment_cubemap.Bind();
            glTexStorage2D(GL_TEXTURE_CUBE_MAP,
                           static_cast<GLsizei>(env_cubemap_mip_levels),
                           GL_RGBA16F,
                           static_cast<GLsizei>(bake_settings.env_cubemap_size),
                           static_cast<GLsizei>(bake_settings.env_cubemap_size));
            cached.environment_cubemap.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            cached.environment_cubemap.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            cached.environment_cubemap.SetParameter(GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            cached.environment_cubemap.SetParameter(GL_TEXTURE_BASE_LEVEL, 0);
            cached.environment_cubemap.SetParameter(GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(env_cubemap_mip_levels - 1u));
            cached.environment_cubemap.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            cached.environment_cubemap.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            if (const GLenum allocation_error = glGetError(); allocation_error != GL_NO_ERROR)
            {
                LOG_ERROR("[HdriPrecomputePass] Cubemap allocation failed with GL error {}", static_cast<unsigned>(allocation_error));
                cached.state = IBLBakeState::Failed;
                return true;
            }
        }

        if (!m_impl->capture_fbo.IsValid() && !m_impl->capture_fbo.Create())
        {
            cached.state = IBLBakeState::Failed;
            return true;
        }

        if (!m_impl->cube_vao.IsValid() && !m_impl->cube_vao.Create())
        {
            cached.state = IBLBakeState::Failed;
            return true;
        }

        const glm::mat4 capture_projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
        const std::array<glm::mat4, 6> capture_views = {
            glm::lookAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
            glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        };

        cached.state = IBLBakeState::Baking;

        m_impl->capture_fbo.Bind();
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        m_impl->capture_fbo.SetDrawBuffers({GL_COLOR_ATTACHMENT0});

        glViewport(0,
                   0,
                   static_cast<GLsizei>(bake_settings.env_cubemap_size),
                   static_cast<GLsizei>(bake_settings.env_cubemap_size));
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        m_equirect_to_cubemap_shader->Use();
        m_equirect_to_cubemap_shader->SetUniform1i("u_equirectangular_map", 0);
        m_equirect_to_cubemap_shader->SetUniformMat4("u_projection", capture_projection);
        source_it->second.texture.BindToUnit(0);
        m_impl->cube_vao.Bind();

        for (int face_index = 0; face_index < 6; ++face_index)
        {
            m_equirect_to_cubemap_shader->SetUniformMat4("u_view", capture_views[static_cast<size_t>(face_index)]);
            m_impl->capture_fbo.AttachTexture2D(GL_COLOR_ATTACHMENT0,
                                                cached.environment_cubemap,
                                                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face_index,
                                                0);
            if (!m_impl->capture_fbo.CheckComplete(GL_FRAMEBUFFER))
            {
                cached.state = IBLBakeState::Failed;
                GLVertexArray::Unbind();
                GLShaderProgram::Unuse();
                GLTexture::Unbind(GL_TEXTURE_2D);
                GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
                glDepthMask(GL_TRUE);
                return true;
            }

            const GLfloat clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            glClearBufferfv(GL_COLOR, 0, clear_color);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        cached.environment_cubemap.Bind();
        cached.environment_cubemap.GenerateMipmap();

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        GLTexture::Unbind(GL_TEXTURE_2D);
        GLTexture::Unbind(GL_TEXTURE_CUBE_MAP);
        GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
        glDepthMask(GL_TRUE);

        // ----------------------- CONVOLUTION --------------------
        if (!cached.convoluted_cubemap.IsValid())
        {
            if (!cached.convoluted_cubemap.Create(GL_TEXTURE_CUBE_MAP))
            {
                cached.state = IBLBakeState::Failed;
                return true;
            }

            cached.convoluted_cubemap.Bind();
            glTexStorage2D(GL_TEXTURE_CUBE_MAP,
                           1,
                           GL_RGBA16F,
                           static_cast<GLsizei>(bake_settings.irradiance_cubemap_size),
                           static_cast<GLsizei>(bake_settings.irradiance_cubemap_size));
            cached.convoluted_cubemap.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            cached.convoluted_cubemap.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            cached.convoluted_cubemap.SetParameter(GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            cached.convoluted_cubemap.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            cached.convoluted_cubemap.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            if (const GLenum allocation_error = glGetError(); allocation_error != GL_NO_ERROR)
            {
                LOG_ERROR("[HdriPrecomputePass] Convoluted cubemap allocation failed with GL error {}", static_cast<unsigned>(allocation_error));
                cached.state = IBLBakeState::Failed;
                return true;
            }
        }

        m_impl->capture_fbo.Bind();
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        m_impl->capture_fbo.SetDrawBuffers({GL_COLOR_ATTACHMENT0});

        glViewport(0,
                   0,
                   static_cast<GLsizei>(bake_settings.irradiance_cubemap_size),
                   static_cast<GLsizei>(bake_settings.irradiance_cubemap_size));
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        m_convolute_shader->Use();
        m_convolute_shader->SetUniform1i("u_env_map", 0);
        m_convolute_shader->SetUniformMat4("u_projection", capture_projection);
        cached.environment_cubemap.BindToUnit(0);
        m_impl->cube_vao.Bind();

        for (int face_index = 0; face_index < 6; ++face_index)
        {
            m_convolute_shader->SetUniformMat4("u_view", capture_views[static_cast<size_t>(face_index)]);
            m_impl->capture_fbo.AttachTexture2D(GL_COLOR_ATTACHMENT0,
                                                cached.convoluted_cubemap,
                                                GL_TEXTURE_CUBE_MAP_POSITIVE_X + face_index,
                                                0);
            if (!m_impl->capture_fbo.CheckComplete(GL_FRAMEBUFFER))
            {
                cached.state = IBLBakeState::Failed;
                GLVertexArray::Unbind();
                GLShaderProgram::Unuse();
                GLTexture::Unbind(GL_TEXTURE_CUBE_MAP);
                GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
                glDepthMask(GL_TRUE);
                return true;
            }

            const GLfloat clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            glClearBufferfv(GL_COLOR, 0, clear_color);
            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        GLTexture::Unbind(GL_TEXTURE_CUBE_MAP);
        GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
        glDepthMask(GL_TRUE);

        // ----------------------- PREFILTER (SPECULAR IBL) --------------------
        if (!cached.prefiltered_cubemap.IsValid())
        {
            if (!cached.prefiltered_cubemap.Create(GL_TEXTURE_CUBE_MAP))
            {
                cached.state = IBLBakeState::Failed;
                return true;
            }

            cached.prefiltered_cubemap.Bind();
            glTexStorage2D(GL_TEXTURE_CUBE_MAP,
                           static_cast<GLsizei>(bake_settings.prefilter_mip_levels),
                           GL_RGBA16F,
                           static_cast<GLsizei>(bake_settings.prefilter_cubemap_size),
                           static_cast<GLsizei>(bake_settings.prefilter_cubemap_size));
            cached.prefiltered_cubemap.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            cached.prefiltered_cubemap.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            cached.prefiltered_cubemap.SetParameter(GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            cached.prefiltered_cubemap.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            cached.prefiltered_cubemap.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            if (const GLenum allocation_error = glGetError(); allocation_error != GL_NO_ERROR)
            {
                LOG_ERROR("[HdriPrecomputePass] Prefilter cubemap allocation failed with GL error {}", static_cast<unsigned>(allocation_error));
                cached.state = IBLBakeState::Failed;
                return true;
            }
        }

        m_impl->capture_fbo.Bind();
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        m_impl->capture_fbo.SetDrawBuffers({GL_COLOR_ATTACHMENT0});

        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        m_prefilter_shader->Use();
        m_prefilter_shader->SetUniform1i("u_env_map", 0);
        m_prefilter_shader->SetUniformMat4("u_projection", capture_projection);
        m_prefilter_shader->SetUniform1f("u_env_map_resolution", static_cast<float>(bake_settings.env_cubemap_size));
        m_prefilter_shader->SetUniform1f("u_env_map_max_mip", static_cast<float>(env_cubemap_mip_levels - 1u));
        cached.environment_cubemap.BindToUnit(0);
        m_impl->cube_vao.Bind();

        for (uint32_t mip_level = 0; mip_level < bake_settings.prefilter_mip_levels; ++mip_level)
        {
            const uint32_t mip_size = bake_settings.prefilter_cubemap_size >> mip_level;
            if (mip_size == 0)
            {
                break;
            }

            glViewport(0, 0, static_cast<GLsizei>(mip_size), static_cast<GLsizei>(mip_size));
            const float roughness = (bake_settings.prefilter_mip_levels > 1)
                                        ? static_cast<float>(mip_level) / static_cast<float>(bake_settings.prefilter_mip_levels - 1)
                                        : 0.0f;
            m_prefilter_shader->SetUniform1f("u_roughness", roughness);

            for (int face_index = 0; face_index < 6; ++face_index)
            {
                m_prefilter_shader->SetUniformMat4("u_view", capture_views[static_cast<size_t>(face_index)]);
                m_impl->capture_fbo.AttachTexture2D(GL_COLOR_ATTACHMENT0,
                                                    cached.prefiltered_cubemap,
                                                    GL_TEXTURE_CUBE_MAP_POSITIVE_X + face_index,
                                                    static_cast<GLint>(mip_level));
                if (!m_impl->capture_fbo.CheckComplete(GL_FRAMEBUFFER))
                {
                    cached.state = IBLBakeState::Failed;
                    GLVertexArray::Unbind();
                    GLShaderProgram::Unuse();
                    GLTexture::Unbind(GL_TEXTURE_CUBE_MAP);
                    GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
                    glDepthMask(GL_TRUE);
                    return true;
                }

                const GLfloat clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                glClearBufferfv(GL_COLOR, 0, clear_color);
                glDrawArrays(GL_TRIANGLES, 0, 36);
            }
        }

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        GLTexture::Unbind(GL_TEXTURE_CUBE_MAP);
        GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
        glDepthMask(GL_TRUE);

        // ----------------------- BRDF LUT (SPECULAR IBL) --------------------
        if (!cached.brdf_lut.IsValid())
        {
            if (!cached.brdf_lut.Create(GL_TEXTURE_2D))
            {
                cached.state = IBLBakeState::Failed;
                return true;
            }

            cached.brdf_lut.Bind();
            cached.brdf_lut.SetImage2D(0,
                                       GL_RG16F,
                                       static_cast<GLsizei>(bake_settings.brdf_lut_size),
                                       static_cast<GLsizei>(bake_settings.brdf_lut_size),
                                       GL_RG,
                                       GL_FLOAT,
                                       nullptr);
            cached.brdf_lut.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            cached.brdf_lut.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            cached.brdf_lut.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            cached.brdf_lut.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            if (const GLenum allocation_error = glGetError(); allocation_error != GL_NO_ERROR)
            {
                LOG_ERROR("[HdriPrecomputePass] BRDF LUT allocation failed with GL error {}", static_cast<unsigned>(allocation_error));
                cached.state = IBLBakeState::Failed;
                return true;
            }
        }

        m_impl->capture_fbo.Bind();
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
        m_impl->capture_fbo.SetDrawBuffers({GL_COLOR_ATTACHMENT0});
        m_impl->capture_fbo.AttachTexture2D(GL_COLOR_ATTACHMENT0, cached.brdf_lut, GL_TEXTURE_2D, 0);
        if (!m_impl->capture_fbo.CheckComplete(GL_FRAMEBUFFER))
        {
            cached.state = IBLBakeState::Failed;
            GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
            return true;
        }

        glViewport(0,
                   0,
                   static_cast<GLsizei>(bake_settings.brdf_lut_size),
                   static_cast<GLsizei>(bake_settings.brdf_lut_size));
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        m_brdf_lut_shader->Use();
        m_impl->cube_vao.Bind();
        const GLfloat clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        glClearBufferfv(GL_COLOR, 0, clear_color);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        GLFramebuffer::BindDefault(GL_FRAMEBUFFER);
        glDepthMask(GL_TRUE);

        LOG_INFO("[HdriPrecomputePass] Done precomputing HDRI light.");

        cached.state = IBLBakeState::Ready;

        output.has_skybox = true;
        output.skybox_cubemap = cached.environment_cubemap.Id();
        output.convoluted_cubemap = cached.convoluted_cubemap.Id();
        output.prefiltered_cubemap = cached.prefiltered_cubemap.Id();
        output.brdf_lut = cached.brdf_lut.Id();
        output.skybox_intensity = selected_light->intensity;
        output.skybox_yaw_radians = selected_light->yaw_radians;
        return true;
    }

} // namespace hybrid::renderer
