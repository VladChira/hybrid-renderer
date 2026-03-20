#include "renderer/Renderer.h"

#include "core/Log.h"
#include "graphics/GraphicsRuntime.h"
#include "renderer/RenderPass.h"
#include "renderer/SceneWorldSnapshot.h"
#include "renderer/ShaderManager.h"
#include "renderer/opengl/GLFramebuffer.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLTexture.h"
#include "renderer/passes/ForwardLitPass.h"

#include <algorithm>
#include <chrono>

#include <GLFW/glfw3.h>

namespace hybrid::renderer
{

    namespace
    {
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

        RendererOutputHandle ToOutputHandle(GLuint texture_id)
        {
            RendererOutputHandle handle{};
            handle.value = static_cast<uint64_t>(texture_id);
            return handle;
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

        LinearPassRunner pass_runner{};
        FrameContext frame_context{};
        const core::scene::SceneWorld *submitted_scene_world = nullptr;
        RenderView submitted_view{};
        RenderSettings submitted_settings{};
        FrameSceneData scene_data{};
        RenderView effective_view{};

        bool initialized = false;
        std::chrono::steady_clock::time_point frame_start{};
    };

    namespace
    {
        void ConfigurePassGraph(Renderer::Impl &impl)
        {
            impl.pass_runner.Clear();
            impl.pass_runner.AddPass(std::make_unique<ForwardLitPass>(&impl.forward_shader));
        }
    } // namespace

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

        ConfigurePassGraph(*m_impl);

        if (m_impl->current_extent.IsValid())
        {
            if (!AllocateRenderTargets(m_impl->current_extent,
                                       m_impl->scene_framebuffer,
                                       m_impl->scene_color,
                                       m_impl->scene_depth))
            {
                return false;
            }

            m_impl->outputs.color = ToOutputHandle(m_impl->scene_color.Id());
            m_impl->outputs.depth = ToOutputHandle(m_impl->scene_depth.Id());
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
        m_impl->scene_data = {};
        m_impl->effective_view = {};
        m_impl->pass_runner.Clear();
        m_impl->forward_shader.Destroy();
        m_impl->scene_color.Destroy();
        m_impl->scene_depth.Destroy();
        m_impl->scene_framebuffer.Destroy();
        m_impl->current_extent = {};
        m_impl->submitted_scene_world = nullptr;
        m_impl->submitted_view = {};
        m_impl->submitted_settings = {};
        m_impl->frame_context = {};
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

        m_impl->outputs.color = ToOutputHandle(m_impl->scene_color.Id());
        m_impl->outputs.depth = ToOutputHandle(m_impl->scene_depth.Id());
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
        m_impl->frame_context = frame;
        m_impl->submitted_scene_world = nullptr;
        m_impl->submitted_view = {};
        m_impl->submitted_settings = {};
        m_impl->scene_data = {};
        m_impl->effective_view = {};

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

        const RendererOutputHandle color_handle = ToOutputHandle(m_impl->scene_color.Id());
        const RendererOutputHandle depth_handle = ToOutputHandle(m_impl->scene_depth.Id());
        m_impl->outputs.color = color_handle;
        m_impl->outputs.depth = depth_handle;

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
        if (!m_impl->initialized)
        {
            return;
        }

        m_impl->submitted_scene_world = &scene_world;
        m_impl->submitted_view = view;
        m_impl->submitted_settings = settings;
    }

    RendererOutputs Renderer::EndFrame()
    {
        if (!m_impl->initialized)
        {
            return {};
        }

        if (m_impl->submitted_scene_world != nullptr)
        {
            m_impl->scene_data = BuildFrameSceneData(*m_impl->submitted_scene_world);
        }
        else
        {
            m_impl->scene_data = {};
        }

        m_impl->effective_view = m_impl->submitted_view;

        m_impl->stats.submitted_mesh_instances =
            static_cast<uint32_t>(m_impl->scene_data.opaque_mesh_instances.size() +
                                  m_impl->scene_data.masked_mesh_instances.size() +
                                  m_impl->scene_data.blended_mesh_instances.size());

        RendererOutputs pass_outputs{};
        PassContext pass_context{};
        pass_context.inputs.settings = &m_impl->submitted_settings;
        pass_context.scene_data = &m_impl->scene_data;
        pass_context.effective_view = &m_impl->effective_view;
        pass_context.stats = &m_impl->stats;
        pass_context.targets.scene_color = ToOutputHandle(m_impl->scene_color.Id());
        pass_context.targets.scene_depth = ToOutputHandle(m_impl->scene_depth.Id());
        pass_context.outputs = &pass_outputs;

        if (!m_impl->pass_runner.Execute(pass_context))
        {
            LOG_ERROR("[Renderer] Pass execution failed");
        }

        if (!pass_outputs.color.IsValid())
        {
            pass_outputs.color = ToOutputHandle(m_impl->scene_color.Id());
        }
        if (!pass_outputs.depth.IsValid())
        {
            pass_outputs.depth = ToOutputHandle(m_impl->scene_depth.Id());
        }
        m_impl->outputs = pass_outputs;

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
