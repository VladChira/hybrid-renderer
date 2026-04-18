#include "renderer/Renderer.h"

#include "core/Log.h"
#include "core/Profiling.h"
#include "graphics/GraphicsRuntime.h"
#include "renderer/FrameResources.h"
#include "renderer/GeometryStore.h"
#include "renderer/LightStore.h"
#include "renderer/MaterialStore.h"
#include "renderer/OpenGLRenderBackend.h"
#include "renderer/SceneWorldSnapshot.h"
#include "renderer/ShaderManager.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLTexture.h"
#include "renderer/raytracing/AccelerationStructureCache.h"

#include "renderer/passes/AreaLightDebugPass.h"
#include "renderer/passes/DeferredLightingPass.h"
#include "renderer/passes/GBufferPass.h"
#include "renderer/passes/HdriPrecomputePass.h"
#include "renderer/passes/RayTracedAlbedoPass.h"
#include "renderer/passes/RayTracedShadowPass.h"
#include "renderer/passes/ShadowDenoisePass.h"
#include "renderer/passes/SsgiDenoisePass.h"
#include "renderer/passes/SsgiTracePass.h"
#include "renderer/passes/TraversalHeatmapPass.h"

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
            outputs.raytrace_heatmap = resources.Get(FrameTarget::RaytraceHeatmap);
            outputs.raytrace_albedo = resources.Get(FrameTarget::RaytraceAlbedo);
            outputs.ssgi_raw = resources.Get(FrameTarget::SsgiRaw);
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
        GLShaderProgram prefilter_hdri_shader{};
        GLShaderProgram brdf_lut_shader{};
        GLShaderProgram traversal_heatmap_shader{};
        GLShaderProgram raytrace_albedo_shader{};
        GLShaderProgram raytrace_shadow_shader{};
        GLShaderProgram shadow_temporal_shader{};
        GLShaderProgram shadow_atrous_shader{};
        GLShaderProgram area_light_debug_shader{};
        GLShaderProgram ssgi_trace_shader{};
        GLShaderProgram ssgi_temporal_shader{};
        GLShaderProgram ssgi_atrous_shader{};
        FrameResources frame_resources{};
        OpenGLRenderBackend backend{};
        GeometryStore geometry_store{};
        MaterialStore material_store{};
        LightStore    light_store{};
        raytracing::AccelerationStructureCache as_cache{};

        std::unique_ptr<GBufferPass> gbuffer_pass{};
        std::unique_ptr<DeferredLightingPass> deferred_lighting_pass{};
        std::unique_ptr<HdriPrecomputePass> hdri_precompute_pass{};
        std::unique_ptr<TraversalHeatmapPass> traversal_heatmap_pass{};
        std::unique_ptr<RayTracedAlbedoPass>  raytrace_albedo_pass{};
        std::unique_ptr<RayTracedShadowPass>  raytrace_shadow_pass{};
        std::unique_ptr<ShadowDenoisePass>    shadow_denoise_pass{};
        std::unique_ptr<AreaLightDebugPass>   area_light_debug_pass{};
        std::unique_ptr<SsgiTracePass>        ssgi_trace_pass{};
        std::unique_ptr<SsgiDenoisePass>      ssgi_denoise_pass{};
        SceneFrameCache scene_frame_cache{};

        // Temporal reprojection state. On frame 0 `prev_view_valid` is false
        // and the temporal shader falls back to the current sample.
        glm::mat4 prev_view_projection{1.0f};
        bool      prev_view_valid = false;

        FrameContext frame_context{};
        core::scene::SceneWorld *submitted_scene_world = nullptr;
        RenderView submitted_view{};
        RenderSettings submitted_settings{};
        FrameSceneData scene_data{};
        RenderView effective_view{};

        bool initialized = false;
        std::chrono::steady_clock::time_point frame_start{};
        bool tracy_gpu_context_initialized = false;
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
        HYBRID_PROFILE_ZONE_N("Renderer::Init");

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

        if (!GLTexture::IsBindlessTextureSupported())
        {
            LOG_ERROR("[Renderer] Init failed: GL_ARB_bindless_texture is required for the unified material path");
            return false;
        }

        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        if (!m_impl->tracy_gpu_context_initialized)
        {
            HYBRID_PROFILE_GL_CONTEXT();
            m_impl->tracy_gpu_context_initialized = true;
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

        if (!m_impl->shader_manager.CompileProgramFromFiles("convolute_hdri.vert",
                                                            "prefilter_hdri.frag",
                                                            m_impl->prefilter_hdri_shader))
        {
            LOG_ERROR("[Renderer] Init failed: prefilter-HDRI shader program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileProgramFromFiles("deferred_lighting.vert",
                                                            "brdf_lut.frag",
                                                            m_impl->brdf_lut_shader))
        {
            LOG_ERROR("[Renderer] Init failed: BRDF LUT shader program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileComputeProgramFromFile("traversal_heatmap.comp",
                                                                   m_impl->traversal_heatmap_shader))
        {
            LOG_ERROR("[Renderer] Init failed: traversal heatmap compute program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileComputeProgramFromFile("raytrace_albedo.comp",
                                                                   m_impl->raytrace_albedo_shader))
        {
            LOG_ERROR("[Renderer] Init failed: raytrace albedo compute program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileComputeProgramFromFile("raytrace_shadow.comp",
                                                                   m_impl->raytrace_shadow_shader))
        {
            LOG_ERROR("[Renderer] Init failed: raytrace shadow compute program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileComputeProgramFromFile("rt/denoise/shadow_temporal.comp",
                                                                   m_impl->shadow_temporal_shader))
        {
            LOG_ERROR("[Renderer] Init failed: shadow temporal denoise program build failed");
            return false;
        }
        if (!m_impl->shader_manager.CompileComputeProgramFromFile("rt/denoise/shadow_atrous.comp",
                                                                   m_impl->shadow_atrous_shader))
        {
            LOG_ERROR("[Renderer] Init failed: shadow à-trous denoise program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileProgramFromFiles("area_light_debug.vert",
                                                             "area_light_debug.frag",
                                                             m_impl->area_light_debug_shader))
        {
            LOG_ERROR("[Renderer] Init failed: area light debug program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileComputeProgramFromFile("rt/ssgi/ssgi_trace.comp",
                                                                   m_impl->ssgi_trace_shader))
        {
            LOG_ERROR("[Renderer] Init failed: SSGI trace program build failed");
            return false;
        }
        if (!m_impl->shader_manager.CompileComputeProgramFromFile("rt/ssgi/ssgi_temporal.comp",
                                                                   m_impl->ssgi_temporal_shader))
        {
            LOG_ERROR("[Renderer] Init failed: SSGI temporal program build failed");
            return false;
        }
        if (!m_impl->shader_manager.CompileComputeProgramFromFile("rt/ssgi/ssgi_atrous.comp",
                                                                   m_impl->ssgi_atrous_shader))
        {
            LOG_ERROR("[Renderer] Init failed: SSGI à-trous program build failed");
            return false;
        }

        if (!m_impl->geometry_store.Init())
        {
            LOG_ERROR("[Renderer] Init failed: geometry store initialization failed");
            return false;
        }
        if (!m_impl->material_store.Init())
        {
            LOG_ERROR("[Renderer] Init failed: material store initialization failed");
            return false;
        }
        if (!m_impl->light_store.Init())
        {
            LOG_ERROR("[Renderer] Init failed: light store initialization failed");
            return false;
        }
        if (!m_impl->as_cache.Init())
        {
            LOG_ERROR("[Renderer] Init failed: acceleration structure cache initialization failed");
            return false;
        }

        m_impl->gbuffer_pass = std::make_unique<GBufferPass>(&m_impl->gbuffer_shader,
                                                             &m_impl->geometry_store,
                                                             &m_impl->material_store);
        m_impl->deferred_lighting_pass = std::make_unique<DeferredLightingPass>(&m_impl->deferred_lighting_shader,
                                                                                 &m_impl->light_store);
        m_impl->hdri_precompute_pass = std::make_unique<HdriPrecomputePass>(&m_impl->equirect_to_cubemap_shader,
                                                                             &m_impl->convolute_hdri_shader,
                                                                             &m_impl->prefilter_hdri_shader,
                                                                             &m_impl->brdf_lut_shader);
        m_impl->traversal_heatmap_pass = std::make_unique<TraversalHeatmapPass>(&m_impl->traversal_heatmap_shader,
                                                                                  &m_impl->geometry_store,
                                                                                  &m_impl->as_cache);
        m_impl->raytrace_albedo_pass   = std::make_unique<RayTracedAlbedoPass>(&m_impl->raytrace_albedo_shader,
                                                                                &m_impl->geometry_store,
                                                                                &m_impl->material_store,
                                                                                &m_impl->as_cache);
        m_impl->raytrace_shadow_pass   = std::make_unique<RayTracedShadowPass>(&m_impl->raytrace_shadow_shader,
                                                                                &m_impl->geometry_store,
                                                                                &m_impl->as_cache);
        m_impl->shadow_denoise_pass    = std::make_unique<ShadowDenoisePass>(&m_impl->shadow_temporal_shader,
                                                                              &m_impl->shadow_atrous_shader);
        m_impl->area_light_debug_pass  = std::make_unique<AreaLightDebugPass>(&m_impl->area_light_debug_shader);
        m_impl->ssgi_trace_pass        = std::make_unique<SsgiTracePass>(&m_impl->ssgi_trace_shader,
                                                                           &m_impl->geometry_store,
                                                                           &m_impl->material_store,
                                                                           &m_impl->as_cache);
        m_impl->ssgi_denoise_pass      = std::make_unique<SsgiDenoisePass>(&m_impl->ssgi_temporal_shader,
                                                                             &m_impl->ssgi_atrous_shader);

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
        m_impl->traversal_heatmap_pass.reset();
        m_impl->raytrace_albedo_pass.reset();
        m_impl->raytrace_shadow_pass.reset();
        m_impl->shadow_denoise_pass.reset();
        m_impl->area_light_debug_pass.reset();
        m_impl->ssgi_trace_pass.reset();
        m_impl->ssgi_denoise_pass.reset();
        m_impl->geometry_store.Clear();
        m_impl->material_store.Clear();
        m_impl->light_store.Clear();
        m_impl->as_cache.Clear();
        m_impl->gbuffer_shader.Destroy();
        m_impl->deferred_lighting_shader.Destroy();
        m_impl->equirect_to_cubemap_shader.Destroy();
        m_impl->convolute_hdri_shader.Destroy();
        m_impl->prefilter_hdri_shader.Destroy();
        m_impl->brdf_lut_shader.Destroy();
        m_impl->traversal_heatmap_shader.Destroy();
        m_impl->raytrace_albedo_shader.Destroy();
        m_impl->raytrace_shadow_shader.Destroy();
        m_impl->shadow_temporal_shader.Destroy();
        m_impl->shadow_atrous_shader.Destroy();
        m_impl->area_light_debug_shader.Destroy();
        m_impl->ssgi_trace_shader.Destroy();
        m_impl->ssgi_temporal_shader.Destroy();
        m_impl->ssgi_atrous_shader.Destroy();
        m_impl->prev_view_valid = false;
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
        HYBRID_PROFILE_ZONE_N("Renderer::BeginFrame");

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

    void Renderer::SubmitScene(core::scene::SceneWorld &scene_world,
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
        HYBRID_PROFILE_ZONE_N("Renderer::EndFrame");

        if (!m_impl->initialized)
        {
            return {};
        }

        if (m_impl->submitted_scene_world != nullptr)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::BuildFrameSceneData");
            m_impl->scene_frame_cache.Sync(*m_impl->submitted_scene_world);
            m_impl->scene_data = m_impl->scene_frame_cache.GetFrameData();
        }
        else
        {
            m_impl->scene_data = {};
            m_impl->scene_frame_cache.Reset();
        }

        m_impl->effective_view = m_impl->submitted_view;
        m_impl->stats.submitted_mesh_instances =
            static_cast<uint32_t>(m_impl->scene_data.opaque_mesh_instances.size() +
                                  m_impl->scene_data.masked_mesh_instances.size() +
                                  m_impl->scene_data.blended_mesh_instances.size());
        m_impl->stats.submitted_primitives = 0;
        m_impl->stats.submitted_vertices = 0;
        m_impl->stats.submitted_triangles = 0;
        {
            auto accumulate_mesh_batch = [this](const std::vector<RenderMeshInstance> &instances)
            {
                for (const RenderMeshInstance &instance : instances)
                {
                    const core::scene::MeshAsset *mesh = instance.mesh.Get();
                    if (mesh == nullptr)
                    {
                        continue;
                    }

                    for (const core::scene::MeshPrimitive &primitive : mesh->primitives)
                    {
                        m_impl->stats.submitted_primitives++;
                        m_impl->stats.submitted_vertices += static_cast<uint64_t>(primitive.vertices.size());
                        m_impl->stats.submitted_triangles += static_cast<uint64_t>(primitive.indices.size() / 3u);
                    }
                }
            };

            accumulate_mesh_batch(m_impl->scene_data.opaque_mesh_instances);
            accumulate_mesh_batch(m_impl->scene_data.masked_mesh_instances);
            accumulate_mesh_batch(m_impl->scene_data.blended_mesh_instances);
        }

        m_impl->outputs = BuildOutputs(m_impl->frame_resources);

        if (m_impl->gbuffer_pass)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::GBufferPass");
            GBufferPassInput gbuffer_input{};
            gbuffer_input.settings = &m_impl->submitted_settings;
            gbuffer_input.scene_data = &m_impl->scene_data;
            gbuffer_input.effective_view = &m_impl->effective_view;
            gbuffer_input.renderer_stats = &m_impl->stats;
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

        {
            HYBRID_PROFILE_ZONE_N("Renderer::AccelerationStructureSync");
            m_impl->as_cache.SyncBlas(m_impl->scene_data, m_impl->geometry_store);
            m_impl->as_cache.SyncTlas(m_impl->scene_data, m_impl->geometry_store);
            // Primitive descriptors may have been patched with BLAS offsets —
            // re-sync so the SSBO reflects those fields for the ray pass.
            m_impl->geometry_store.Sync();
            m_impl->as_cache.Upload();
        }

        // Light upload happens before the shadow pass so ShadowCasters() is
        // populated, and before deferred lighting so light SSBOs are current.
        m_impl->light_store.Update(m_impl->scene_data,
                                    m_impl->submitted_settings.enable_raytrace_shadows);

        if (m_impl->raytrace_shadow_pass)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::RayTracedShadowPass");
            RayTracedShadowPassInput shadow_input{};
            shadow_input.settings          = &m_impl->submitted_settings;
            shadow_input.effective_view    = &m_impl->effective_view;
            shadow_input.light_store       = &m_impl->light_store;
            shadow_input.gbuffer_depth     = m_impl->frame_resources.Get(FrameTarget::GBufferDepth);
            shadow_input.gbuffer_rt1       = m_impl->frame_resources.Get(FrameTarget::GBufferRt1);
            shadow_input.shadow_mask_array = m_impl->frame_resources.Get(FrameTarget::RaytraceShadowMasks);
            shadow_input.frame_index       = static_cast<uint32_t>(m_impl->frame_context.frame_index);
            if (!m_impl->raytrace_shadow_pass->Execute(shadow_input))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->raytrace_shadow_pass->Name());
            }
        }

        GlTextureId final_shadow_mask = m_impl->frame_resources.Get(FrameTarget::RaytraceShadowMasks);
        const bool run_denoise = m_impl->submitted_settings.enable_raytrace_shadows &&
                                 m_impl->submitted_settings.enable_shadow_denoise &&
                                 m_impl->shadow_denoise_pass != nullptr;
        if (run_denoise)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::ShadowDenoisePass");
            const uint32_t ping = static_cast<uint32_t>(m_impl->frame_context.frame_index) & 1u;
            const FrameTarget history_current_target =
                (ping == 0u) ? FrameTarget::RaytraceShadowHistory0 : FrameTarget::RaytraceShadowHistory1;
            const FrameTarget history_prev_target =
                (ping == 0u) ? FrameTarget::RaytraceShadowHistory1 : FrameTarget::RaytraceShadowHistory0;

            ShadowDenoisePassInput denoise_input{};
            denoise_input.settings          = &m_impl->submitted_settings;
            denoise_input.effective_view    = &m_impl->effective_view;
            denoise_input.light_store       = &m_impl->light_store;
            denoise_input.gbuffer_depth     = m_impl->frame_resources.Get(FrameTarget::GBufferDepth);
            denoise_input.gbuffer_rt1       = m_impl->frame_resources.Get(FrameTarget::GBufferRt1);
            denoise_input.shadow_mask_array = m_impl->frame_resources.Get(FrameTarget::RaytraceShadowMasks);
            denoise_input.history_current   = m_impl->frame_resources.Get(history_current_target);
            denoise_input.history_prev      = m_impl->frame_resources.Get(history_prev_target);
            denoise_input.filter_ping[0]    = m_impl->frame_resources.Get(FrameTarget::RaytraceShadowFilter0);
            denoise_input.filter_ping[1]    = m_impl->frame_resources.Get(FrameTarget::RaytraceShadowFilter1);
            denoise_input.prev_view_projection = m_impl->prev_view_projection;
            denoise_input.history_valid     = m_impl->prev_view_valid;

            ShadowDenoisePassOutput denoise_output{};
            if (!m_impl->shadow_denoise_pass->Execute(denoise_input, denoise_output))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->shadow_denoise_pass->Name());
            }
            else if (denoise_output.filtered_mask_array != 0)
            {
                final_shadow_mask = denoise_output.filtered_mask_array;
            }
        }

        if (m_impl->traversal_heatmap_pass)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::TraversalHeatmapPass");
            TraversalHeatmapPassInput heatmap_input{};
            heatmap_input.settings = &m_impl->submitted_settings;
            heatmap_input.effective_view = &m_impl->effective_view;
            heatmap_input.heatmap_texture = m_impl->frame_resources.Get(FrameTarget::RaytraceHeatmap);
            if (!m_impl->traversal_heatmap_pass->Execute(heatmap_input))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->traversal_heatmap_pass->Name());
            }
        }

        if (m_impl->raytrace_albedo_pass)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::RayTracedAlbedoPass");
            RayTracedAlbedoPassInput albedo_input{};
            albedo_input.settings = &m_impl->submitted_settings;
            albedo_input.effective_view = &m_impl->effective_view;
            albedo_input.albedo_texture = m_impl->frame_resources.Get(FrameTarget::RaytraceAlbedo);
            if (!m_impl->raytrace_albedo_pass->Execute(albedo_input))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->raytrace_albedo_pass->Name());
            }
        }

        // Precompute any new/stale HDRIs here before we shade.
        HdriPrecomputePassOutput hdri_output{};
        if (m_impl->hdri_precompute_pass)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::HdriPrecomputePass");
            HdriPrecomputePassInput hdri_input{};
            hdri_input.scene_data = &m_impl->scene_data;
            hdri_input.settings = &m_impl->submitted_settings;

            if (!m_impl->hdri_precompute_pass->Execute(hdri_input, hdri_output))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->hdri_precompute_pass->Name());
            }
        }
        
        // Scene radiance ping-pong. `current` is the slot deferred lighting
        // writes this frame; `prev` is what SSGI samples as last-frame
        // radiance.
        const uint32_t radiance_ping = static_cast<uint32_t>(m_impl->frame_context.frame_index) & 1u;
        const FrameTarget radiance_current_target = (radiance_ping == 0u) ? FrameTarget::SceneRadiance0 : FrameTarget::SceneRadiance1;
        const FrameTarget radiance_prev_target    = (radiance_ping == 0u) ? FrameTarget::SceneRadiance1 : FrameTarget::SceneRadiance0;

        const GLuint radiance_current_id = static_cast<GLuint>(m_impl->frame_resources.Get(radiance_current_target));
        const GLuint radiance_prev_id    = static_cast<GLuint>(m_impl->frame_resources.Get(radiance_prev_target));
        const GLuint scene_fbo_id        = static_cast<GLuint>(m_impl->frame_resources.GetFbo(FrameFramebuffer::Scene));
        if (radiance_current_id != 0 && scene_fbo_id != 0)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_id);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, radiance_current_id, 0);
        }

        // ---- SSGI: trace → denoise → feed to deferred lighting -----------
        GlTextureId ssgi_filtered_for_deferred = 0;
        if (m_impl->submitted_settings.enable_ssgi && m_impl->ssgi_trace_pass)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::SsgiTracePass");
            SsgiTracePassInput ssgi_input{};
            ssgi_input.settings            = &m_impl->submitted_settings;
            ssgi_input.effective_view      = &m_impl->effective_view;
            ssgi_input.gbuffer_depth       = m_impl->frame_resources.Get(FrameTarget::GBufferDepth);
            ssgi_input.gbuffer_rt1         = m_impl->frame_resources.Get(FrameTarget::GBufferRt1);
            ssgi_input.scene_radiance_prev = radiance_prev_id;
            ssgi_input.irradiance_cubemap  = hdri_output.convoluted_cubemap;
            ssgi_input.has_irradiance      = hdri_output.has_skybox && hdri_output.convoluted_cubemap != 0;
            ssgi_input.skybox_intensity    = hdri_output.skybox_intensity;
            ssgi_input.skybox_yaw_radians  = hdri_output.skybox_yaw_radians;
            ssgi_input.ssgi_raw_texture    = m_impl->frame_resources.Get(FrameTarget::SsgiRaw);
            ssgi_input.frame_index         = static_cast<uint32_t>(m_impl->frame_context.frame_index);
            if (!m_impl->ssgi_trace_pass->Execute(ssgi_input))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->ssgi_trace_pass->Name());
            }

            if (m_impl->submitted_settings.enable_ssgi_denoise && m_impl->ssgi_denoise_pass)
            {
                HYBRID_PROFILE_ZONE_N("Renderer::SsgiDenoisePass");
                const uint32_t ssgi_ping = static_cast<uint32_t>(m_impl->frame_context.frame_index) & 1u;
                const FrameTarget ssgi_hist_curr = (ssgi_ping == 0u) ? FrameTarget::SsgiHistory0 : FrameTarget::SsgiHistory1;
                const FrameTarget ssgi_hist_prev = (ssgi_ping == 0u) ? FrameTarget::SsgiHistory1 : FrameTarget::SsgiHistory0;

                SsgiDenoisePassInput dn_input{};
                dn_input.settings        = &m_impl->submitted_settings;
                dn_input.effective_view  = &m_impl->effective_view;
                dn_input.gbuffer_depth   = m_impl->frame_resources.Get(FrameTarget::GBufferDepth);
                dn_input.gbuffer_rt1     = m_impl->frame_resources.Get(FrameTarget::GBufferRt1);
                dn_input.ssgi_raw        = m_impl->frame_resources.Get(FrameTarget::SsgiRaw);
                dn_input.history_current = m_impl->frame_resources.Get(ssgi_hist_curr);
                dn_input.history_prev    = m_impl->frame_resources.Get(ssgi_hist_prev);
                dn_input.filter_ping[0]  = m_impl->frame_resources.Get(FrameTarget::SsgiFilter0);
                dn_input.filter_ping[1]  = m_impl->frame_resources.Get(FrameTarget::SsgiFilter1);
                dn_input.prev_view_projection = m_impl->prev_view_projection;
                dn_input.history_valid   = m_impl->prev_view_valid;

                SsgiDenoisePassOutput dn_output{};
                if (!m_impl->ssgi_denoise_pass->Execute(dn_input, dn_output))
                {
                    LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->ssgi_denoise_pass->Name());
                }
                ssgi_filtered_for_deferred = dn_output.filtered;
            }
            else
            {
                ssgi_filtered_for_deferred = m_impl->frame_resources.Get(FrameTarget::SsgiRaw);
            }
        }

        if (m_impl->submitted_settings.mode == RenderMode::Lit && m_impl->deferred_lighting_pass)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::DeferredLightingPass");
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
            deferred_input.prefiltered_cubemap = hdri_output.prefiltered_cubemap;
            deferred_input.brdf_lut = hdri_output.brdf_lut;
            deferred_input.skybox_intensity = hdri_output.skybox_intensity;
            deferred_input.skybox_yaw_radians = hdri_output.skybox_yaw_radians;
            deferred_input.shadow_mask_array = final_shadow_mask;
            deferred_input.ssgi_texture      = ssgi_filtered_for_deferred;
            m_impl->outputs.ssgi_filtered    = ssgi_filtered_for_deferred;

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

            if (m_impl->area_light_debug_pass)
            {
                HYBRID_PROFILE_ZONE_N("Renderer::AreaLightDebugPass");
                AreaLightDebugPassInput debug_input{};
                debug_input.settings             = &m_impl->submitted_settings;
                debug_input.scene_data           = &m_impl->scene_data;
                debug_input.effective_view       = &m_impl->effective_view;
                debug_input.scene_framebuffer_id = m_impl->frame_resources.GetFbo(FrameFramebuffer::Scene);
                debug_input.gbuffer_depth        = m_impl->frame_resources.Get(FrameTarget::GBufferDepth);
                if (!m_impl->area_light_debug_pass->Execute(debug_input))
                {
                    LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->area_light_debug_pass->Name());
                }
            }
        }

        // Record this frame's view matrices for next-frame reprojection in
        // any temporal denoiser (shadows, SSGI, etc.).
        m_impl->prev_view_projection = m_impl->effective_view.projection * m_impl->effective_view.view;
        m_impl->prev_view_valid      = true;

        m_impl->backend.EndFrame();
        HYBRID_PROFILE_GL_COLLECT();

        const auto frame_end = std::chrono::steady_clock::now();
        m_impl->stats.cpu_frame_ms =
            std::chrono::duration<double, std::milli>(frame_end - m_impl->frame_start).count();
        return m_impl->outputs;
    }

    const RendererStats &Renderer::GetStats() const
    {
        return m_impl->stats;
    }

    const raytracing::AccelerationStructureStats *Renderer::GetAccelerationStructureStats() const
    {
        return &m_impl->as_cache.Stats();
    }

} // namespace hybrid::renderer
