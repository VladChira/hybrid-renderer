#include "renderer/Renderer.h"

#include "core/Log.h"
#include "graphics/GraphicsRuntime.h"
#include "renderer/ShaderManager.h"
#include "renderer/opengl/GLFramebuffer.h"
#include "renderer/opengl/GLTexture.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLVertexArray.h"
#include "renderer/opengl/GLBuffer.h"
#include "renderer/SceneWorldSnapshot.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

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

        bool IsMatrixIdentity(const glm::mat4 &matrix, float epsilon = 1e-4f)
        {
            const glm::mat4 identity(1.0f);
            for (int col = 0; col < 4; ++col)
            {
                for (int row = 0; row < 4; ++row)
                {
                    if (std::fabs(matrix[col][row] - identity[col][row]) > epsilon)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        void MergeBounds(core::scene::Aabb &accumulated, const core::scene::Aabb &bounds)
        {
            if (!bounds.valid)
            {
                return;
            }

            if (!accumulated.valid)
            {
                accumulated = bounds;
                return;
            }

            accumulated.min = glm::min(accumulated.min, bounds.min);
            accumulated.max = glm::max(accumulated.max, bounds.max);
            accumulated.valid = true;
        }

        void BuildFallbackView(const RenderSceneSnapshot &scene,
                               const RenderExtent &extent,
                               RenderView &inout_view)
        {
            constexpr glm::vec3 kFallbackCameraPosition(-3.0f, 2.76f, 0.24f);

            core::scene::Aabb scene_bounds{};
            for (const auto &instance : scene.mesh_instances)
            {
                MergeBounds(scene_bounds, instance.world_bounds);
            }

            const float width = std::max(extent.width, 1u);
            const float height = std::max(extent.height, 1u);
            const float aspect = width / height;

            if (!scene_bounds.valid)
            {
                inout_view.position = kFallbackCameraPosition;
                inout_view.view = glm::lookAt(inout_view.position,
                                              glm::vec3(0.0f),
                                              glm::vec3(0.0f, 1.0f, 0.0f));
                inout_view.projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
                return;
            }

            const glm::vec3 center = 0.5f * (scene_bounds.min + scene_bounds.max);
            const glm::vec3 extent_vec = 0.5f * (scene_bounds.max - scene_bounds.min);
            const float radius = std::max(glm::length(extent_vec), 1.0f);

            inout_view.position = kFallbackCameraPosition;
            inout_view.view = glm::lookAt(inout_view.position, center, glm::vec3(0.0f, 1.0f, 0.0f));
            inout_view.projection = glm::perspective(glm::radians(60.0f), aspect, 0.1f, radius * 12.0f + 100.0f);
        }

        glm::vec3 ResolvePrimitiveBaseColor(const core::scene::MeshPrimitive &primitive)
        {
            if (const auto *material = primitive.material.Get())
            {
                return glm::vec3(material->base_color_factor);
            }
            return glm::vec3(0.8f);
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

            GLVertexArray::Unbind();
            GLBuffer::Unbind(GL_ARRAY_BUFFER);
            GLBuffer::Unbind(GL_ELEMENT_ARRAY_BUFFER);

            out_gpu.index_count = static_cast<GLsizei>(primitive.indices.size());
            return true;
        }

        bool AllocateRenderTargets(const RenderExtent &extent,
                                   GLFramebuffer &framebuffer,
                                   GLTexture &color_texture,
                                   GLTexture &depth_texture)
        {
            if (!extent.IsValid())
            {
                return false;
            }

            if (!framebuffer.IsValid() && !framebuffer.Create())
            {
                LOG_ERROR("[Renderer] Failed to create scene framebuffer");
                return false;
            }

            if (!color_texture.IsValid() && !color_texture.Create(GL_TEXTURE_2D))
            {
                LOG_ERROR("[Renderer] Failed to create scene color texture");
                return false;
            }

            if (!depth_texture.IsValid() && !depth_texture.Create(GL_TEXTURE_2D))
            {
                LOG_ERROR("[Renderer] Failed to create scene depth texture");
                return false;
            }

            color_texture.Bind();
            color_texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            color_texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            color_texture.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            color_texture.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            color_texture.SetImage2D(0,
                                     GL_RGBA8,
                                     static_cast<GLsizei>(extent.width),
                                     static_cast<GLsizei>(extent.height),
                                     GL_RGBA,
                                     GL_UNSIGNED_BYTE,
                                     nullptr);

            depth_texture.Bind();
            depth_texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            depth_texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            depth_texture.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            depth_texture.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            depth_texture.SetImage2D(0,
                                     GL_DEPTH_COMPONENT24,
                                     static_cast<GLsizei>(extent.width),
                                     static_cast<GLsizei>(extent.height),
                                     GL_DEPTH_COMPONENT,
                                     GL_FLOAT,
                                     nullptr);

            framebuffer.Bind(GL_FRAMEBUFFER);
            framebuffer.AttachTexture2D(GL_COLOR_ATTACHMENT0, color_texture);
            framebuffer.AttachTexture2D(GL_DEPTH_ATTACHMENT, depth_texture);
            framebuffer.SetDrawBuffers({GL_COLOR_ATTACHMENT0});
            const bool complete = framebuffer.CheckComplete(GL_FRAMEBUFFER);
            GLFramebuffer::BindDefault(GL_FRAMEBUFFER);

            if (!complete)
            {
                LOG_ERROR("[Renderer] Scene framebuffer is incomplete");
                return false;
            }

            return true;
        }
    } // namespace

    struct Renderer::Impl
    {
        RenderExtent current_extent{};
        RendererStats stats{};
        RendererOutputs outputs{};
        ShaderManager shader_manager{};
        GLShaderProgram forward_shader{};
        GLFramebuffer scene_framebuffer{};
        GLTexture scene_color{};
        GLTexture scene_depth{};
        std::unordered_map<PrimitiveCacheKey, CachedPrimitiveGpu, PrimitiveCacheKeyHash> primitive_cache;
        bool initialized = false;
        std::chrono::steady_clock::time_point frame_start{};
    };

    Renderer::Renderer()
        : m_impl(std::make_unique<Impl>())
    {
    }

    Renderer::~Renderer()
    {
        Shutdown();
    }

    bool Renderer::Init()
    {
        if (glfwGetCurrentContext() == nullptr)
        {
            LOG_ERROR("[Renderer] Init failed: no current OpenGL context");
            return false;
        }

        if (!graphics::EnsureOpenGLInitialized(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            LOG_ERROR("[Renderer] Init failed: OpenGL runtime initialization failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileProgramFromFiles("forward.vert",
                                                            "forward.frag",
                                                            m_impl->forward_shader))
        {
            LOG_ERROR("[Renderer] Init failed: forward shader program build failed");
            return false;
        }

        if (m_impl->current_extent.IsValid())
        {
            if (!AllocateRenderTargets(m_impl->current_extent,
                                       m_impl->scene_framebuffer,
                                       m_impl->scene_color,
                                       m_impl->scene_depth))
            {
                return false;
            }

            m_impl->outputs.color.value = static_cast<uint64_t>(m_impl->scene_color.Id());
            m_impl->outputs.depth.value = static_cast<uint64_t>(m_impl->scene_depth.Id());
        }

        m_impl->initialized = true;
        return true;
    }

    void Renderer::Shutdown()
    {
        if (!m_impl->initialized)
        {
            return;
        }

        m_impl->initialized = false;
        m_impl->stats = {};
        m_impl->outputs = {};
        m_impl->forward_shader.Destroy();
        m_impl->primitive_cache.clear();
        m_impl->scene_color.Destroy();
        m_impl->scene_depth.Destroy();
        m_impl->scene_framebuffer.Destroy();
        m_impl->current_extent = {};
    }

    void Renderer::Resize(const RenderExtent &extent)
    {
        m_impl->current_extent = extent;
        if (!m_impl->initialized || !m_impl->current_extent.IsValid())
        {
            return;
        }

        if (!AllocateRenderTargets(m_impl->current_extent,
                                   m_impl->scene_framebuffer,
                                   m_impl->scene_color,
                                   m_impl->scene_depth))
        {
            return;
        }

        m_impl->outputs.color.value = static_cast<uint64_t>(m_impl->scene_color.Id());
        m_impl->outputs.depth.value = static_cast<uint64_t>(m_impl->scene_depth.Id());
    }

    bool Renderer::BeginFrame(const FrameContext &frame)
    {
        if (!m_impl->initialized)
        {
            return false;
        }

        m_impl->stats = {};
        m_impl->frame_start = std::chrono::steady_clock::now();
        m_impl->current_extent = frame.render_extent;

        if (!m_impl->current_extent.IsValid())
        {
            return false;
        }

        if (!AllocateRenderTargets(m_impl->current_extent,
                                   m_impl->scene_framebuffer,
                                   m_impl->scene_color,
                                   m_impl->scene_depth))
        {
            return false;
        }

        m_impl->outputs.color.value = static_cast<uint64_t>(m_impl->scene_color.Id());
        m_impl->outputs.depth.value = static_cast<uint64_t>(m_impl->scene_depth.Id());

        m_impl->scene_framebuffer.Bind(GL_FRAMEBUFFER);
        glViewport(0, 0,
                   static_cast<GLsizei>(m_impl->current_extent.width),
                   static_cast<GLsizei>(m_impl->current_extent.height));
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        return true;
    }

    void Renderer::SubmitScene(const core::scene::SceneWorld &scene_world,
                               const RenderView &view,
                               const RenderSettings &settings)
    {
        RenderSceneSnapshot scene = renderer::BuildRenderSceneSnapshot(scene_world);
        m_impl->stats.submitted_mesh_instances =
            static_cast<uint32_t>(scene.mesh_instances.size());

        RenderView effective_view = view;
        const bool use_fallback_view = IsMatrixIdentity(effective_view.view) ||
                                       IsMatrixIdentity(effective_view.projection);
        if (use_fallback_view)
        {
            BuildFallbackView(scene, m_impl->current_extent, effective_view);
        }

        m_impl->forward_shader.Use();
        m_impl->forward_shader.SetUniformMat4("u_view", effective_view.view);
        m_impl->forward_shader.SetUniformMat4("u_projection", effective_view.projection);
        m_impl->forward_shader.SetUniformVec3("u_camera_position", effective_view.position);
        m_impl->forward_shader.SetUniform1i("u_render_mode", static_cast<int>(settings.mode));

        const bool wireframe = settings.mode == RenderMode::Wireframe;
        if (wireframe)
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
        else
        {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }

        for (const auto &instance : scene.mesh_instances)
        {
            const core::scene::MeshAsset *mesh = instance.mesh.Get();
            if (mesh == nullptr)
            {
                continue;
            }

            for (size_t primitive_index = 0; primitive_index < mesh->primitives.size(); ++primitive_index)
            {
                const core::scene::MeshPrimitive &primitive = mesh->primitives[primitive_index];
                m_impl->stats.submitted_primitives++;
                m_impl->stats.submitted_vertices += primitive.vertices.size();
                m_impl->stats.submitted_triangles += primitive.indices.size() / 3;

                PrimitiveCacheKey key{};
                key.mesh_id = instance.mesh.Id().value;
                key.primitive_index = static_cast<uint32_t>(primitive_index);

                auto gpu_it = m_impl->primitive_cache.find(key);
                if (gpu_it == m_impl->primitive_cache.end())
                {
                    CachedPrimitiveGpu cached_primitive{};
                    if (!UploadPrimitiveToGpu(primitive, cached_primitive))
                    {
                        continue;
                    }
                    gpu_it = m_impl->primitive_cache.emplace(key, std::move(cached_primitive)).first;
                }

                CachedPrimitiveGpu &gpu = gpu_it->second;
                if (gpu.index_count == 0)
                {
                    continue;
                }

                m_impl->forward_shader.SetUniformMat4("u_model", instance.world_from_local);
                m_impl->forward_shader.SetUniformVec3("u_base_color", ResolvePrimitiveBaseColor(primitive));

                gpu.vao.Bind();
                glDrawElements(GL_TRIANGLES, gpu.index_count, GL_UNSIGNED_INT, nullptr);
            }
        }

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    RendererOutputs Renderer::EndFrame()
    {
        if (!m_impl->initialized)
        {
            return {};
        }

        GLFramebuffer::BindDefault(GL_FRAMEBUFFER);

        const auto frame_end = std::chrono::steady_clock::now();
        m_impl->stats.cpu_frame_ms =
            std::chrono::duration<double, std::milli>(frame_end - m_impl->frame_start).count();
        return m_impl->outputs;
    }

    const RendererStats &Renderer::GetStats() const
    {
        return m_impl->stats;
    }

} // namespace hybrid::renderer
