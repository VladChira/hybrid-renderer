#include "renderer/passes/HdriPrecomputePass.h"

#include "assets/ImageAsset.h"
#include "core/Log.h"
#include "renderer/opengl/GLFramebuffer.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLTexture.h"
#include "renderer/opengl/GLVertexArray.h"

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
        constexpr uint32_t kSkyboxCubemapSize = 512;
        constexpr uint32_t kConvolutionSize = 32;

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
    };

    HdriPrecomputePass::HdriPrecomputePass(GLShaderProgram *equirect_to_cubemap_shader, GLShaderProgram *convolute_shader)
        : m_equirect_to_cubemap_shader(equirect_to_cubemap_shader),
          m_convolute_shader(convolute_shader),
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
        if (m_impl == nullptr || m_equirect_to_cubemap_shader == nullptr || m_convolute_shader == nullptr || input.scene_data == nullptr)
        {
            return false;
        }

        output = {};

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
            cached.convoluted_cubemap.IsValid())
        {
            output.has_skybox = true;
            output.skybox_cubemap = cached.environment_cubemap.Id();
            output.convoluted_cubemap = cached.convoluted_cubemap.Id();
            output.skybox_intensity = selected_light->intensity;
            output.skybox_yaw_radians = selected_light->yaw_radians;
            return true;
        }

        LOG_INFO("[HdriPrecomputePass] Starting HDRI precompute for light {} (texture id {}).",
                 selected_light->instance_id,
                 source_texture_id);

        if (!cached.environment_cubemap.IsValid())
        {
            if (!cached.environment_cubemap.Create(GL_TEXTURE_CUBE_MAP))
            {
                cached.state = IBLBakeState::Failed;
                return true;
            }

            cached.environment_cubemap.Bind();
            glTexStorage2D(GL_TEXTURE_CUBE_MAP,
                           1,
                           GL_RGBA16F,
                           static_cast<GLsizei>(kSkyboxCubemapSize),
                           static_cast<GLsizei>(kSkyboxCubemapSize));
            cached.environment_cubemap.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            cached.environment_cubemap.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            cached.environment_cubemap.SetParameter(GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            cached.environment_cubemap.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
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

        glViewport(0, 0, static_cast<GLsizei>(kSkyboxCubemapSize), static_cast<GLsizei>(kSkyboxCubemapSize));
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

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        GLTexture::Unbind(GL_TEXTURE_2D);
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
                           static_cast<GLsizei>(kConvolutionSize),
                           static_cast<GLsizei>(kConvolutionSize));
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

        glViewport(0, 0, static_cast<GLsizei>(kConvolutionSize), static_cast<GLsizei>(kConvolutionSize));
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

        LOG_INFO("[HdriPrecomputePass] Done precomputing HDRI light.");

        cached.state = IBLBakeState::Ready;

        output.has_skybox = true;
        output.skybox_cubemap = cached.environment_cubemap.Id();
        output.convoluted_cubemap = cached.convoluted_cubemap.Id();
        output.skybox_intensity = selected_light->intensity;
        output.skybox_yaw_radians = selected_light->yaw_radians;
        return true;
    }

} // namespace hybrid::renderer
