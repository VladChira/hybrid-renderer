#include "renderer/Renderer.h"

#include "core/Log.h"
#include "core/Profiling.h"
#include "graphics/GraphicsRuntime.h"
#include "renderer/FrameResources.h"
#include "renderer/stores/GeometryStore.h"
#include "renderer/stores/MaterialStore.h"
#include "renderer/stores/LightStore.h"
#include "renderer/OpenGLRenderBackend.h"
#include "renderer/SceneWorldSnapshot.h"
#include "renderer/ShaderManager.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/raytracing/AccelerationStructureCache.h"

#include "renderer/passes/DeferredLightingPass.h"
#include "renderer/passes/GBufferPass.h"
#include "renderer/passes/HdriPrecomputePass.h"
#include "renderer/passes/AreaLightVisualizationPass.h"
#include "renderer/passes/RenderTargetChannelsPass.h"
#include "renderer/passes/TraversalHeatmapPass.h"
#include "renderer/passes/RayTracedShadowPass.h"
#include "renderer/passes/SpatioTemporalDenoisePass.h"

#include <array>
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
            outputs.color_channels.rgb = resources.Get(FrameTarget::SceneColorRgb);
            outputs.color_channels.r = resources.Get(FrameTarget::SceneColorR);
            outputs.color_channels.g = resources.Get(FrameTarget::SceneColorG);
            outputs.color_channels.b = resources.Get(FrameTarget::SceneColorB);
            outputs.color_channels.a = resources.Get(FrameTarget::SceneColorA);
            outputs.gbuffer_rt0_channels.rgb = resources.Get(FrameTarget::GBufferRt0Rgb);
            outputs.gbuffer_rt0_channels.r = resources.Get(FrameTarget::GBufferRt0R);
            outputs.gbuffer_rt0_channels.g = resources.Get(FrameTarget::GBufferRt0G);
            outputs.gbuffer_rt0_channels.b = resources.Get(FrameTarget::GBufferRt0B);
            outputs.gbuffer_rt0_channels.a = resources.Get(FrameTarget::GBufferRt0A);
            outputs.gbuffer_rt1_channels.rgb = resources.Get(FrameTarget::GBufferRt1Rgb);
            outputs.gbuffer_rt1_channels.r = resources.Get(FrameTarget::GBufferRt1R);
            outputs.gbuffer_rt1_channels.g = resources.Get(FrameTarget::GBufferRt1G);
            outputs.gbuffer_rt1_channels.b = resources.Get(FrameTarget::GBufferRt1B);
            outputs.gbuffer_rt1_channels.a = resources.Get(FrameTarget::GBufferRt1A);
            outputs.raytrace_heatmap = resources.Get(FrameTarget::RaytraceHeatmap);
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
        GLShaderProgram extract_channel_shader{};
        GLShaderProgram area_light_visualization_shader{};
        GLShaderProgram traversal_heatmap_shader{};
        GLShaderProgram raytrace_shadow_shader{};
        GLShaderProgram temporal_accumulation_shader{};
        GLShaderProgram atrous_denoise_shader{};
        FrameResources frame_resources{};
        OpenGLRenderBackend backend{};
        GeometryStore geometry_store{};
        MaterialStore material_store{};
        LightStore light_store{};
        raytracing::AccelerationStructureCache as_cache{};

        std::unique_ptr<GBufferPass> gbuffer_pass{};
        std::unique_ptr<DeferredLightingPass> deferred_lighting_pass{};
        std::unique_ptr<HdriPrecomputePass> hdri_precompute_pass{};
        std::unique_ptr<AreaLightVisualizationPass> area_light_visualization_pass{};
        std::unique_ptr<RenderTargetChannelsPass> render_target_channels_pass{};
        std::unique_ptr<TraversalHeatmapPass> traversal_heatmap_pass{};
        std::unique_ptr<RayTracedShadowPass> raytrace_shadow_pass{};
        std::unique_ptr<SpatioTemporalDenoisePass> shadow_denoise_pass{};
        SceneFrameCache scene_frame_cache{};

        FrameContext frame_context{};
        core::scene::SceneWorld *submitted_scene_world = nullptr;
        RenderView submitted_view{};
        RenderSettings submitted_settings{};
        FrameSceneData scene_data{};
        RenderView effective_view{};

        bool initialized = false;
        std::chrono::steady_clock::time_point frame_start{};
        bool tracy_gpu_context_initialized = false;
        glm::mat4 prev_view_projection{1.0f};
        bool prev_view_projection_valid = false;
        bool prev_gbuffer_valid = false;
        bool shadow_history_prev_is_a = true;
        bool shadow_history_valid = false;
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

        if (!m_impl->shader_manager.CompileProgramFromFiles("deferred_lighting.vert",
                                                            "extract_channel.frag",
                                                            m_impl->extract_channel_shader))
        {
            LOG_ERROR("[Renderer] Init failed: channel extraction shader program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileProgramFromFiles("area_light_visualization.vert",
                                                            "area_light_visualization.frag",
                                                            m_impl->area_light_visualization_shader))
        {
            LOG_ERROR("[Renderer] Init failed: area-light visualization shader program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileComputeProgramFromFile("compute/traversal_heatmap.comp",
                                                                  m_impl->traversal_heatmap_shader))
        {
            LOG_ERROR("[Renderer] Init failed: traversal heatmap compute program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileComputeProgramFromFile("compute/raytrace_shadow.comp",
                                                                  m_impl->raytrace_shadow_shader))
        {
            LOG_ERROR("[Renderer] Init failed: raytrace shadow compute program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileComputeProgramFromFile("compute/temporal_accumulation.comp",
                                                                  m_impl->temporal_accumulation_shader))
        {
            LOG_ERROR("[Renderer] Init failed: temporal accumulation compute program build failed");
            return false;
        }

        if (!m_impl->shader_manager.CompileComputeProgramFromFile("compute/atrous_denoise.comp",
                                                                  m_impl->atrous_denoise_shader))
        {
            LOG_ERROR("[Renderer] Init failed: atrous denoise compute program build failed");
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
        m_impl->area_light_visualization_pass = std::make_unique<AreaLightVisualizationPass>(&m_impl->area_light_visualization_shader);
        m_impl->render_target_channels_pass = std::make_unique<RenderTargetChannelsPass>(&m_impl->extract_channel_shader);
        m_impl->traversal_heatmap_pass = std::make_unique<TraversalHeatmapPass>(&m_impl->traversal_heatmap_shader,
                                                                                &m_impl->geometry_store,
                                                                                &m_impl->as_cache);
        m_impl->raytrace_shadow_pass = std::make_unique<RayTracedShadowPass>(&m_impl->raytrace_shadow_shader,
                                                                             &m_impl->geometry_store,
                                                                             &m_impl->as_cache);
        m_impl->shadow_denoise_pass = std::make_unique<SpatioTemporalDenoisePass>(&m_impl->temporal_accumulation_shader,
                                                                                   &m_impl->atrous_denoise_shader);

        LOG_INFO("[Renderer] Current rendering passes:");
        LOG_INFO("[Renderer] \t - Hdri Precompute pass [OpenGL Raster]");
        LOG_INFO("[Renderer] \t - GBuffer Pass [OpenGL Raster]");
        LOG_INFO("[Renderer] \t - Deferred Lighting Pass [OpenGL Raster]");
        LOG_INFO("[Renderer] \t - Area Light Visualization Pass [OpenGL Raster]");
        LOG_INFO("[Renderer] \t - Render Target Channels Pass [OpenGL Raster]");
        LOG_INFO("[Renderer] \t - Ray tracing heatmap visualization [OpenGL Compute Shader]");
        LOG_INFO("[Renderer] \t - Ray traced shadows [OpenGL Compute Shader]");
        LOG_INFO("[Renderer] \t - Generic spatio-temporal denoise [OpenGL Compute Shader]");

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
        m_impl->area_light_visualization_pass.reset();
        m_impl->render_target_channels_pass.reset();
        m_impl->geometry_store.Clear();
        m_impl->material_store.Clear();
        m_impl->as_cache.Clear();
        m_impl->light_store.Clear();
        m_impl->gbuffer_shader.Destroy();
        m_impl->deferred_lighting_shader.Destroy();
        m_impl->equirect_to_cubemap_shader.Destroy();
        m_impl->convolute_hdri_shader.Destroy();
        m_impl->prefilter_hdri_shader.Destroy();
        m_impl->brdf_lut_shader.Destroy();
        m_impl->extract_channel_shader.Destroy();
        m_impl->area_light_visualization_shader.Destroy();
        m_impl->traversal_heatmap_shader.Destroy();
        m_impl->raytrace_shadow_shader.Destroy();
        m_impl->temporal_accumulation_shader.Destroy();
        m_impl->atrous_denoise_shader.Destroy();
        m_impl->frame_resources.Reset();
        m_impl->traversal_heatmap_pass.reset();
        m_impl->raytrace_shadow_pass.reset();
        m_impl->shadow_denoise_pass.reset();
        m_impl->current_extent = {};
        m_impl->submitted_scene_world = nullptr;
        m_impl->submitted_view = {};
        m_impl->submitted_settings = {};
        m_impl->frame_context = {};
        m_impl->prev_view_projection = glm::mat4(1.0f);
        m_impl->prev_view_projection_valid = false;
        m_impl->prev_gbuffer_valid = false;
        m_impl->shadow_history_prev_is_a = true;
        m_impl->shadow_history_valid = false;
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
        m_impl->shadow_history_valid = false;
        m_impl->shadow_history_prev_is_a = true;
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

        const RenderExtent previous_extent = m_impl->frame_resources.Extent();
        const bool had_valid_resources = m_impl->frame_resources.IsValid();
        if (!m_impl->frame_resources.Resize(m_impl->current_extent))
        {
            return false;
        }

        const bool resized_this_frame =
            !had_valid_resources ||
            previous_extent.width != m_impl->current_extent.width ||
            previous_extent.height != m_impl->current_extent.height;
        if (resized_this_frame)
        {
            m_impl->shadow_history_valid = false;
            m_impl->shadow_history_prev_is_a = true;
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
            // Primitive descriptors may have been patched with BLAS offsets -
            // re-sync so the SSBO reflects those fields for ray passes.
            m_impl->geometry_store.Sync();
            if (!m_impl->as_cache.Upload())
            {
                LOG_ERROR("[Renderer] Acceleration structure upload failed");
            }
        }

        const bool should_compute_bvh_heatmap =
            m_impl->submitted_settings.compute_bvh_heatmap &&
            (m_impl->traversal_heatmap_pass != nullptr);

        if (should_compute_bvh_heatmap)
        {
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
        }
        else
        {
            const GlTextureId heatmap_texture = m_impl->frame_resources.Get(FrameTarget::RaytraceHeatmap);
            if (heatmap_texture != 0)
            {
                constexpr std::array<GLubyte, 4> kZeroHeatmap = {0, 0, 0, 255};
                glClearTexImage(heatmap_texture, 0, GL_RGBA, GL_UNSIGNED_BYTE, kZeroHeatmap.data());
            }
        }

        // Light upload happens before the shadow pass so ShadowCasters() is
        // populated, and before deferred lighting so light SSBOs are current.
        m_impl->light_store.Update(m_impl->scene_data,
                                    m_impl->submitted_settings.enable_ray_traced_shadows);

        if (m_impl->raytrace_shadow_pass)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::RayTracedShadowPass");
            const GlTextureId shadow_masks = m_impl->frame_resources.Get(FrameTarget::RaytraceShadowMasks);
            if (shadow_masks != 0)
            {
                constexpr GLubyte kLit = 255;
                glClearTexImage(shadow_masks, 0, GL_RED, GL_UNSIGNED_BYTE, &kLit);
            }

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

        uint32_t shadow_denoise_layer_mask = 0u;
        for (const ShadowCaster &caster : m_impl->light_store.ShadowCasters())
        {
            if (caster.type == ShadowCaster::Type::Area && caster.layer < 32u)
            {
                shadow_denoise_layer_mask |= (1u << caster.layer);
            }
        }
        if (m_impl->submitted_settings.enable_ray_traced_hdri_visibility &&
            kRaytraceEnvironmentShadowLayer < 32u)
        {
            shadow_denoise_layer_mask |= (1u << kRaytraceEnvironmentShadowLayer);
        }

        const bool should_run_shadow_denoise =
            m_impl->submitted_settings.enable_shadow_denoise &&
            (m_impl->submitted_settings.enable_ray_traced_shadows ||
             m_impl->submitted_settings.enable_ray_traced_hdri_visibility) &&
            shadow_denoise_layer_mask != 0u;

        GlTextureId resolved_shadow_mask_array = m_impl->frame_resources.Get(FrameTarget::RaytraceShadowMasks);
        if (m_impl->shadow_denoise_pass != nullptr && should_run_shadow_denoise)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::ShadowDenoisePass");
            const GlTextureId history_prev = m_impl->shadow_history_prev_is_a
                                                ? m_impl->frame_resources.Get(FrameTarget::RaytraceShadowHistoryA)
                                                : m_impl->frame_resources.Get(FrameTarget::RaytraceShadowHistoryB);
            const GlTextureId history_out = m_impl->shadow_history_prev_is_a
                                                ? m_impl->frame_resources.Get(FrameTarget::RaytraceShadowHistoryB)
                                                : m_impl->frame_resources.Get(FrameTarget::RaytraceShadowHistoryA);

            SpatioTemporalDenoisePassInput denoise_input{};
            denoise_input.effective_view = &m_impl->effective_view;
            denoise_input.extent = m_impl->submitted_settings.render_extent;
            denoise_input.current_signal_array = m_impl->frame_resources.Get(FrameTarget::RaytraceShadowMasks);
            denoise_input.history_prev_array = history_prev;
            denoise_input.history_out_array = history_out;
            denoise_input.atrous_ping_array = m_impl->frame_resources.Get(FrameTarget::RaytraceShadowAtrousPing);
            denoise_input.atrous_pong_array = m_impl->frame_resources.Get(FrameTarget::RaytraceShadowAtrousPong);
            denoise_input.gbuffer_rt1 = m_impl->frame_resources.Get(FrameTarget::GBufferRt1);
            denoise_input.gbuffer_depth = m_impl->frame_resources.Get(FrameTarget::GBufferDepth);
            denoise_input.gbuffer_rt1_prev = m_impl->frame_resources.Get(FrameTarget::PrevGBufferRt1);
            denoise_input.gbuffer_depth_prev = m_impl->frame_resources.Get(FrameTarget::PrevGBufferDepth);
            denoise_input.layer_count = kRaytraceShadowMaskLayerCount;
            denoise_input.denoise_layer_mask = shadow_denoise_layer_mask;
            denoise_input.history_valid = m_impl->shadow_history_valid &&
                                          m_impl->prev_view_projection_valid &&
                                          m_impl->prev_gbuffer_valid;
            denoise_input.prev_view_projection = m_impl->prev_view_projection;
            denoise_input.camera_near = m_impl->effective_view.near_plane;
            denoise_input.camera_far = m_impl->effective_view.far_plane;
            denoise_input.depth_tolerance = m_impl->submitted_settings.shadow_denoise_depth_tolerance;
            denoise_input.normal_tolerance = m_impl->submitted_settings.shadow_denoise_normal_tolerance;
            denoise_input.temporal_alpha = m_impl->submitted_settings.shadow_denoise_temporal_alpha;
            denoise_input.atrous_iterations = m_impl->submitted_settings.shadow_denoise_atrous_iterations;
            denoise_input.sigma_l = m_impl->submitted_settings.shadow_denoise_sigma_l;
            denoise_input.n_phi = m_impl->submitted_settings.shadow_denoise_n_phi;
            denoise_input.p_phi = m_impl->submitted_settings.shadow_denoise_p_phi;

            SpatioTemporalDenoisePassOutput denoise_output{};
            if (!m_impl->shadow_denoise_pass->Execute(denoise_input, denoise_output))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->shadow_denoise_pass->Name());
                m_impl->shadow_history_valid = false;
                m_impl->shadow_history_prev_is_a = true;
            }
            else
            {
                m_impl->shadow_history_valid = true;
                m_impl->shadow_history_prev_is_a = !m_impl->shadow_history_prev_is_a;
                resolved_shadow_mask_array = denoise_output.denoised;
            }
        }
        else
        {
            m_impl->shadow_history_valid = false;
            m_impl->shadow_history_prev_is_a = true;
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
            deferred_input.shadow_mask_array = resolved_shadow_mask_array;

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

        if (m_impl->area_light_visualization_pass)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::AreaLightVisualizationPass");
            AreaLightVisualizationPassInput area_light_visualization_input{};
            area_light_visualization_input.settings = &m_impl->submitted_settings;
            area_light_visualization_input.scene_data = &m_impl->scene_data;
            area_light_visualization_input.effective_view = &m_impl->effective_view;
            area_light_visualization_input.scene_framebuffer_id = m_impl->frame_resources.GetFbo(FrameFramebuffer::Scene);
            area_light_visualization_input.gbuffer_framebuffer_id = m_impl->frame_resources.GetFbo(FrameFramebuffer::GBuffer);
            if (!m_impl->area_light_visualization_pass->Execute(area_light_visualization_input))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->area_light_visualization_pass->Name());
            }
        }

        if (m_impl->render_target_channels_pass)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::RenderTargetChannelsPass");
            RenderTargetChannelsPassInput channel_input{};
            channel_input.extent = m_impl->current_extent;
            channel_input.debug_framebuffer_id = m_impl->frame_resources.GetFbo(FrameFramebuffer::DebugChannelExtract);
            channel_input.source_color = m_impl->outputs.color;
            channel_input.source_gbuffer_rt0 = m_impl->outputs.gbuffer_rt0;
            channel_input.source_gbuffer_rt1 = m_impl->outputs.gbuffer_rt1;
            channel_input.out_color_channels = m_impl->outputs.color_channels;
            channel_input.out_gbuffer_rt0_channels = m_impl->outputs.gbuffer_rt0_channels;
            channel_input.out_gbuffer_rt1_channels = m_impl->outputs.gbuffer_rt1_channels;
            if (!m_impl->render_target_channels_pass->Execute(channel_input))
            {
                LOG_ERROR("[Renderer] Pass '{}' failed", m_impl->render_target_channels_pass->Name());
            }
        }

        m_impl->backend.EndFrame();
        HYBRID_PROFILE_GL_COLLECT();

        const auto frame_end = std::chrono::steady_clock::now();
        m_impl->stats.cpu_frame_ms =
            std::chrono::duration<double, std::milli>(frame_end - m_impl->frame_start).count();
        m_impl->prev_view_projection = m_impl->effective_view.projection * m_impl->effective_view.view;
        m_impl->prev_view_projection_valid = true;

        const GlTextureId cur_depth = m_impl->frame_resources.Get(FrameTarget::GBufferDepth);
        const GlTextureId prev_depth = m_impl->frame_resources.Get(FrameTarget::PrevGBufferDepth);
        const GlTextureId cur_rt1 = m_impl->frame_resources.Get(FrameTarget::GBufferRt1);
        const GlTextureId prev_rt1 = m_impl->frame_resources.Get(FrameTarget::PrevGBufferRt1);
        const auto &cur_extent = m_impl->current_extent;
        if (cur_depth != 0 && prev_depth != 0 && cur_rt1 != 0 && prev_rt1 != 0 && cur_extent.IsValid())
        {
            glCopyImageSubData(cur_depth, GL_TEXTURE_2D, 0, 0, 0, 0,
                               prev_depth, GL_TEXTURE_2D, 0, 0, 0, 0,
                               static_cast<GLsizei>(cur_extent.width),
                               static_cast<GLsizei>(cur_extent.height),
                               1);
            glCopyImageSubData(cur_rt1, GL_TEXTURE_2D, 0, 0, 0, 0,
                               prev_rt1, GL_TEXTURE_2D, 0, 0, 0, 0,
                               static_cast<GLsizei>(cur_extent.width),
                               static_cast<GLsizei>(cur_extent.height),
                               1);
            m_impl->prev_gbuffer_valid = true;
        }
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
