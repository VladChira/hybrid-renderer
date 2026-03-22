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
#include "renderer/passes/GBufferPass.h"

#include <algorithm>
#include <chrono>

#include <GLFW/glfw3.h>

namespace hybrid::renderer
{

    namespace
    {
        bool AllocateSceneTargets(const RenderExtent &extent,
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

        bool AllocateGBufferTargets(const RenderExtent &extent,
                                    GLFramebuffer &framebuffer,
                                    GLTexture &rt0_texture,
                                    GLTexture &rt1_texture,
                                    GLTexture &entity_id_texture,
                                    GLTexture &depth_texture)
        {
            if (!extent.IsValid())
            {
                return false;
            }

            if (!framebuffer.IsValid() && !framebuffer.Create())
            {
                LOG_ERROR("[Renderer] Failed to create gbuffer framebuffer");
                return false;
            }

            if (!rt0_texture.IsValid() && !rt0_texture.Create(GL_TEXTURE_2D))
            {
                LOG_ERROR("[Renderer] Failed to create gbuffer rt0 texture");
                return false;
            }

            if (!rt1_texture.IsValid() && !rt1_texture.Create(GL_TEXTURE_2D))
            {
                LOG_ERROR("[Renderer] Failed to create gbuffer rt1 texture");
                return false;
            }

            if (!depth_texture.IsValid() && !depth_texture.Create(GL_TEXTURE_2D))
            {
                LOG_ERROR("[Renderer] Failed to create gbuffer depth texture");
                return false;
            }
            if (!entity_id_texture.IsValid() && !entity_id_texture.Create(GL_TEXTURE_2D))
            {
                LOG_ERROR("[Renderer] Failed to create gbuffer entity id texture");
                return false;
            }

            rt0_texture.Bind();
            rt0_texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            rt0_texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            rt0_texture.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            rt0_texture.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            rt0_texture.SetImage2D(0,
                                   GL_RGBA8,
                                   static_cast<GLsizei>(extent.width),
                                   static_cast<GLsizei>(extent.height),
                                   GL_RGBA,
                                   GL_UNSIGNED_BYTE,
                                   nullptr);

            rt1_texture.Bind();
            rt1_texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            rt1_texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            rt1_texture.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            rt1_texture.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            rt1_texture.SetImage2D(0,
                                   GL_RGBA16F,
                                   static_cast<GLsizei>(extent.width),
                                   static_cast<GLsizei>(extent.height),
                                   GL_RGBA,
                                   GL_HALF_FLOAT,
                                   nullptr);

            entity_id_texture.Bind();
            entity_id_texture.SetParameter(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            entity_id_texture.SetParameter(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            entity_id_texture.SetParameter(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            entity_id_texture.SetParameter(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            entity_id_texture.SetImage2D(0,
                                         GL_R32UI,
                                         static_cast<GLsizei>(extent.width),
                                         static_cast<GLsizei>(extent.height),
                                         GL_RED_INTEGER,
                                         GL_UNSIGNED_INT,
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
            framebuffer.AttachTexture2D(GL_COLOR_ATTACHMENT0, rt0_texture);
            framebuffer.AttachTexture2D(GL_COLOR_ATTACHMENT1, rt1_texture);
            framebuffer.AttachTexture2D(GL_COLOR_ATTACHMENT2, entity_id_texture);
            framebuffer.AttachTexture2D(GL_DEPTH_ATTACHMENT, depth_texture);
            framebuffer.SetDrawBuffers({GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2});
            const bool complete = framebuffer.CheckComplete(GL_FRAMEBUFFER);
            GLFramebuffer::BindDefault(GL_FRAMEBUFFER);

            if (!complete)
            {
                LOG_ERROR("[Renderer] GBuffer framebuffer is incomplete");
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
        GLShaderProgram gbuffer_shader{};
        GLShaderProgram forward_shader{};
        GLFramebuffer scene_framebuffer{};
        GLTexture scene_color{};
        GLTexture scene_depth{};
        GLFramebuffer gbuffer_framebuffer{};
        GLTexture gbuffer_rt0{};
        GLTexture gbuffer_rt1{};
        GLTexture gbuffer_entity_id{};
        GLTexture gbuffer_depth{};

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
            impl.pass_runner.AddPass(std::make_unique<GBufferPass>(&impl.gbuffer_shader));
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

        if (!m_impl->shader_manager.CompileProgramFromFiles("gbuffer.vert",
                                                            "gbuffer.frag",
                                                            m_impl->gbuffer_shader))
        {
            LOG_ERROR("[Renderer] Init failed: gbuffer shader program build failed");
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
            if (!AllocateSceneTargets(m_impl->current_extent,
                                      m_impl->scene_framebuffer,
                                      m_impl->scene_color,
                                      m_impl->scene_depth))
            {
                return false;
            }

            if (!AllocateGBufferTargets(m_impl->current_extent,
                                        m_impl->gbuffer_framebuffer,
                                        m_impl->gbuffer_rt0,
                                        m_impl->gbuffer_rt1,
                                        m_impl->gbuffer_entity_id,
                                        m_impl->gbuffer_depth))
            {
                return false;
            }

            m_impl->outputs.color = ToOutputHandle(m_impl->scene_color.Id());
            m_impl->outputs.depth = ToOutputHandle(m_impl->gbuffer_depth.Id());
            m_impl->outputs.gbuffer_rt0 = ToOutputHandle(m_impl->gbuffer_rt0.Id());
            m_impl->outputs.gbuffer_rt1 = ToOutputHandle(m_impl->gbuffer_rt1.Id());
            m_impl->outputs.gbuffer_entity_id = ToOutputHandle(m_impl->gbuffer_entity_id.Id());
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
        m_impl->gbuffer_shader.Destroy();
        m_impl->forward_shader.Destroy();
        m_impl->scene_color.Destroy();
        m_impl->scene_depth.Destroy();
        m_impl->scene_framebuffer.Destroy();
        m_impl->gbuffer_rt0.Destroy();
        m_impl->gbuffer_rt1.Destroy();
        m_impl->gbuffer_entity_id.Destroy();
        m_impl->gbuffer_depth.Destroy();
        m_impl->gbuffer_framebuffer.Destroy();
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

        if (!AllocateSceneTargets(m_impl->current_extent,
                                  m_impl->scene_framebuffer,
                                  m_impl->scene_color,
                                  m_impl->scene_depth))
        {
            return;
        }

        if (!AllocateGBufferTargets(m_impl->current_extent,
                                    m_impl->gbuffer_framebuffer,
                                    m_impl->gbuffer_rt0,
                                    m_impl->gbuffer_rt1,
                                    m_impl->gbuffer_entity_id,
                                    m_impl->gbuffer_depth))
        {
            return;
        }

        m_impl->outputs.color = ToOutputHandle(m_impl->scene_color.Id());
        m_impl->outputs.depth = ToOutputHandle(m_impl->gbuffer_depth.Id());
        m_impl->outputs.gbuffer_rt0 = ToOutputHandle(m_impl->gbuffer_rt0.Id());
        m_impl->outputs.gbuffer_rt1 = ToOutputHandle(m_impl->gbuffer_rt1.Id());
        m_impl->outputs.gbuffer_entity_id = ToOutputHandle(m_impl->gbuffer_entity_id.Id());
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

        if (!AllocateSceneTargets(m_impl->current_extent,
                                  m_impl->scene_framebuffer,
                                  m_impl->scene_color,
                                  m_impl->scene_depth))
        {
            return false;
        }

        if (!AllocateGBufferTargets(m_impl->current_extent,
                                    m_impl->gbuffer_framebuffer,
                                    m_impl->gbuffer_rt0,
                                    m_impl->gbuffer_rt1,
                                    m_impl->gbuffer_entity_id,
                                    m_impl->gbuffer_depth))
        {
            return false;
        }

        const RendererOutputHandle color_handle = ToOutputHandle(m_impl->scene_color.Id());
        const RendererOutputHandle depth_handle = ToOutputHandle(m_impl->gbuffer_depth.Id());
        const RendererOutputHandle gbuffer_rt0_handle = ToOutputHandle(m_impl->gbuffer_rt0.Id());
        const RendererOutputHandle gbuffer_rt1_handle = ToOutputHandle(m_impl->gbuffer_rt1.Id());
        const RendererOutputHandle gbuffer_entity_id_handle = ToOutputHandle(m_impl->gbuffer_entity_id.Id());
        m_impl->outputs.color = color_handle;
        m_impl->outputs.depth = depth_handle;
        m_impl->outputs.gbuffer_rt0 = gbuffer_rt0_handle;
        m_impl->outputs.gbuffer_rt1 = gbuffer_rt1_handle;
        m_impl->outputs.gbuffer_entity_id = gbuffer_entity_id_handle;

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
        pass_context.targets.scene_framebuffer_id = m_impl->scene_framebuffer.Id();
        pass_context.targets.gbuffer_framebuffer_id = m_impl->gbuffer_framebuffer.Id();
        pass_context.targets.scene_color = ToOutputHandle(m_impl->scene_color.Id());
        pass_context.targets.scene_depth = ToOutputHandle(m_impl->scene_depth.Id());
        pass_context.targets.gbuffer_rt0 = ToOutputHandle(m_impl->gbuffer_rt0.Id());
        pass_context.targets.gbuffer_rt1 = ToOutputHandle(m_impl->gbuffer_rt1.Id());
        pass_context.targets.gbuffer_entity_id = ToOutputHandle(m_impl->gbuffer_entity_id.Id());
        pass_context.targets.gbuffer_depth = ToOutputHandle(m_impl->gbuffer_depth.Id());
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
            pass_outputs.depth = ToOutputHandle(m_impl->gbuffer_depth.Id());
        }
        if (!pass_outputs.gbuffer_rt0.IsValid())
        {
            pass_outputs.gbuffer_rt0 = ToOutputHandle(m_impl->gbuffer_rt0.Id());
        }
        if (!pass_outputs.gbuffer_rt1.IsValid())
        {
            pass_outputs.gbuffer_rt1 = ToOutputHandle(m_impl->gbuffer_rt1.Id());
        }
        if (!pass_outputs.gbuffer_entity_id.IsValid())
        {
            pass_outputs.gbuffer_entity_id = ToOutputHandle(m_impl->gbuffer_entity_id.Id());
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
