#include "renderer/Renderer.h"

#include "core/Log.h"
#include "graphics/GraphicsRuntime.h"
#include "renderer/FrameResources.h"
#include "renderer/OpenGLRenderBackend.h"
#include "renderer/SceneWorldSnapshot.h"
#include "renderer/ShaderManager.h"
#include "renderer/opengl/GLShaderProgram.h"

#include "renderer/passes/DeferredLightingPass.h"
#include "renderer/passes/GBufferPass.h"
#include "renderer/passes/HdriPrecomputePass.h"

#include <chrono>

#include <GLFW/glfw3.h>

namespace hybrid::renderer
{

    namespace
    {
        RendererOutputs BuildOutputs(const FrameResources &resources)
        {
            RendererOutputs outputs{};
            outputs.color = resources.Get(FrameTarget::SceneColor);
            outputs.depth = resources.Get(FrameTarget::GBufferDepth);
            outputs.gbuffer_rt0 = resources.Get(FrameTarget::GBufferRt0);
            outputs.gbuffer_rt1 = resources.Get(FrameTarget::GBufferRt1);
            outputs.gbuffer_entity_id = resources.Get(FrameTarget::GBufferEntityId);
            return outputs;
        }
    } // namespace

    struct Renderer::Impl
    {
        RenderExtent current_extent{};
        RendererStats stats{};
        RendererOutputs outputs{};
        ShaderManager shader_manager{};
        GLShaderProgram gbuffer_shader{};
        GLShaderProgram deferred_lighting_shader{};
        GLShaderProgram equirect_to_cubemap_shader{};
        GLShaderProgram convolute_hdri_shader{};
        FrameResources frame_resources{};
        OpenGLRenderBackend backend{};

        std::unique_ptr<GBufferPass> gbuffer_pass{};
        std::unique_ptr<DeferredLightingPass> deferred_lighting_pass{};
        std::unique_ptr<HdriPrecomputePass> hdri_precompute_pass{};

        FrameContext frame_context{};
        const core::scene::SceneWorld *submitted_scene_world = nullptr;
        RenderView submitted_view{};
        RenderSettings submitted_settings{};
        FrameSceneData scene_data{};
        RenderView effective_view{};

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

        if (!m_impl->shader_manager.CompileProgramFromFiles("gbuffer.vert",
                                                            "gbuffer.frag",
                                                            m_impl->gbuffer_shader))
        {
            LOG_ERROR("[Renderer] Init failed: gbuffer shader program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileProgramFromFiles("deferred_lighting.vert",
                                                            "deferred_lighting.frag",
                                                            m_impl->deferred_lighting_shader))
        {
            LOG_ERROR("[Renderer] Init failed: deferred lighting shader program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileProgramFromFiles("equirect_to_cubemap.vert",
                                                            "equirect_to_cubemap.frag",
                                                            m_impl->equirect_to_cubemap_shader))
        {
            LOG_ERROR("[Renderer] Init failed: equirect-to-cubemap shader program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileProgramFromFiles("convolute_hdri.vert",
                                                            "convolute_hdri.frag",
                                                            m_impl->convolute_hdri_shader))
        {
            LOG_ERROR("[Renderer] Init failed: convolute-HDRI shader program build failed");
            return false;
        }

        m_impl->gbuffer_pass = std::make_unique<GBufferPass>(&m_impl->gbuffer_shader);
        m_impl->deferred_lighting_pass = std::make_unique<DeferredLightingPass>(&m_impl->deferred_lighting_shader);
        m_impl->hdri_precompute_pass = std::make_unique<HdriPrecomputePass>(&m_impl->equirect_to_cubemap_shader, &m_impl->convolute_hdri_shader);

        LOG_INFO("[Renderer] Current rendering passes:");
        LOG_INFO("[Renderer] \t - GBuffer Pass [OpenGL Raster]");
        LOG_INFO("[Renderer] \t - Deferred Lighting Pass [OpenGL Fullscreen]");

        if (m_impl->current_extent.IsValid())
        {
            if (!m_impl->frame_resources.Resize(m_impl->current_extent))
            {
                return false;
            }
            m_impl->outputs = BuildOutputs(m_impl->frame_resources);
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
        m_impl->gbuffer_pass.reset();
        m_impl->deferred_lighting_pass.reset();
        m_impl->hdri_precompute_pass.reset();
        m_impl->gbuffer_shader.Destroy();
        m_impl->deferred_lighting_shader.Destroy();
        m_impl->equirect_to_cubemap_shader.Destroy();
        m_impl->convolute_hdri_shader.Destroy();
        m_impl->frame_resources.Reset();
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

        if (!m_impl->frame_resources.Resize(m_impl->current_extent))
        {
            return;
        }

        m_impl->outputs = BuildOutputs(m_impl->frame_resources);
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
        m_impl->submitted_settings.render_extent = frame.render_extent;
        m_impl->scene_data = {};
        m_impl->effective_view = {};

        if (!m_impl->current_extent.IsValid())
        {
            return false;
        }

        if (!m_impl->frame_resources.Resize(m_impl->current_extent))
        {
            return false;
        }

        m_impl->outputs = BuildOutputs(m_impl->frame_resources);
        return m_impl->backend.BeginFrame(
            m_impl->frame_resources.GetFbo(FrameFramebuffer::Scene),
            m_impl->current_extent);
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
        m_impl->submitted_settings.render_extent = m_impl->current_extent;
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

        m_impl->outputs = BuildOutputs(m_impl->frame_resources);

        if (m_impl->gbuffer_pass)
        {
            GBufferPassInput gbuffer_input{};
            gbuffer_input.settings = &m_impl->submitted_settings;
            gbuffer_input.scene_data = &m_impl->scene_data;
            gbuffer_input.effective_view = &m_impl->effective_view;
            gbuffer_input.gbuffer_framebuffer_id = m_impl->frame_resources.GetFbo(FrameFramebuffer::GBuffer);
            gbuffer_input.gbuffer_rt0 = m_impl->frame_resources.Get(FrameTarget::GBufferRt0);
            gbuffer_input.gbuffer_rt1 = m_impl->frame_resources.Get(FrameTarget::GBufferRt1);
            gbuffer_input.gbuffer_entity_id = m_impl->frame_resources.Get(FrameTarget::GBufferEntityId);
            gbuffer_input.gbuffer_depth = m_impl->frame_resources.Get(FrameTarget::GBufferDepth);

            GBufferPassOutput gbuffer_output{};
            if (!m_impl->gbuffer_pass->Execute(gbuffer_input, gbuffer_output))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->gbuffer_pass->Name());
            }
            else
            {
                m_impl->outputs.gbuffer_rt0 = gbuffer_output.gbuffer_rt0;
                m_impl->outputs.gbuffer_rt1 = gbuffer_output.gbuffer_rt1;
                m_impl->outputs.gbuffer_entity_id = gbuffer_output.gbuffer_entity_id;
                m_impl->outputs.depth = gbuffer_output.depth;
            }
        }

        // Precompute any new/stale HDRIs here before we shade.
        HdriPrecomputePassOutput hdri_output{};
        if (m_impl->hdri_precompute_pass)
        {
            HdriPrecomputePassInput hdri_input{};
            hdri_input.scene_data = &m_impl->scene_data;

            if (!m_impl->hdri_precompute_pass->Execute(hdri_input, hdri_output))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->hdri_precompute_pass->Name());
            }
        }
        
        if (m_impl->submitted_settings.mode == RenderMode::Lit && m_impl->deferred_lighting_pass)
        {
            DeferredLightingPassInput deferred_input{};
            deferred_input.settings = &m_impl->submitted_settings;
            deferred_input.scene_data = &m_impl->scene_data;
            deferred_input.effective_view = &m_impl->effective_view;
            deferred_input.scene_framebuffer_id = m_impl->frame_resources.GetFbo(FrameFramebuffer::Scene);
            deferred_input.scene_color = m_impl->frame_resources.Get(FrameTarget::SceneColor);
            deferred_input.gbuffer_rt0 = m_impl->outputs.gbuffer_rt0;
            deferred_input.gbuffer_rt1 = m_impl->outputs.gbuffer_rt1;
            deferred_input.gbuffer_depth = m_impl->outputs.depth;
            deferred_input.has_skybox = hdri_output.has_skybox;
            deferred_input.skybox_cubemap = hdri_output.skybox_cubemap;
            deferred_input.convoluted_cubemap = hdri_output.convoluted_cubemap;
            deferred_input.skybox_intensity = hdri_output.skybox_intensity;
            deferred_input.skybox_yaw_radians = hdri_output.skybox_yaw_radians;

            DeferredLightingPassOutput deferred_output{};
            if (!m_impl->deferred_lighting_pass->Execute(deferred_input, deferred_output))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->deferred_lighting_pass->Name());
            }
            else
            {
                m_impl->outputs.color = deferred_output.color;
                m_impl->outputs.depth = deferred_output.depth;
            }
        }

        m_impl->backend.EndFrame();

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
