#include "core/App.h"

#include <chrono>

#include "assets/AssimpSceneLoader.h"
#include "assets/DiskAssetDataSource.h"
#include "assets/StbImageLoader.h"

#include "core/scene/SceneWorld.h"
#include "core/scene/SceneCameraResolver.h"
#include "core/Log.h"
#include "core/Profiling.h"
#include "core/PerformanceTelemetry.h"
#include "core/UiCommandProcessor.h"

#include "ui/panels/AccelerationStructurePanel.h"

#include "utils/Banner.h"

#include <algorithm>
#include <optional>
#include <thread>

namespace hybrid::core
{
    App::App()
        : m_scene_loader(&m_assets)
    {
    }

    /**
     * Initialize the modules, run the main loop, shutdown in reverse order
     */
    int App::Run(const AppConfig &config)
    {
        Log::Init();
        LOG_INFO("Starting Hybrid Renderer...");
        PerformanceTelemetry::Init();

#if defined(HYBRID_ENABLE_TRACY) && HYBRID_ENABLE_TRACY
        LOG_INFO("Profiling: Tracy ENABLED");
#else
        LOG_INFO("Profiling: Tracy DISABLED");
#endif

        if (const std::string banner = utils::LoadBannerText(); !banner.empty())
        {
           LOG_INFO("\n\n" + banner + "\n\n");
        }
        else
        {
            LOG_ERROR("Banner not found, skipping...");
        }

        LOG_INFO("Starting Platform module...");
        platform::Platform platform;
        if (!platform.Init(config.platform))
        {
            LOG_CRITICAL("Platform module failed to start, aborting...");
            return 1;
        }
        LOG_INFO("Platform module started");

        LOG_INFO("Starting Renderer module...");
        renderer::Renderer renderer;
        if (!renderer.Init())
        {
            LOG_CRITICAL("Renderer module failed to start, aborting...");
            platform.Shutdown();
            return 2;
        }
        LOG_INFO("Renderer module started");

        LOG_INFO("Starting UI module...");
        ui::Ui ui;
        if (!ui.Init(config.ui, platform.GetNativeHandle()))
        {
            LOG_CRITICAL("UI module failed to start, aborting...");
            renderer.Shutdown();
            platform.Shutdown();
            return 3;
        }
        LOG_INFO("UI module started");

        // Cross-module panels that depend on renderer internals.
        // TODO: Don't call this here, bad!
        ui.RegisterPanel(std::make_unique<ui::AccelerationStructurePanel>(
                             renderer.GetAccelerationStructureStats()),
                         ui::DockTarget::LeftBottom);

        LOG_INFO("Starting Asset Manager...");
        m_assets.SetDataSource(std::make_shared<assets::DiskAssetDataSource>());
        m_assets.RegisterLoader(std::make_unique<assets::StbImageLoader>());
        m_assets.RegisterLoader(std::make_unique<assets::AssimpSceneLoader>(&m_assets)); // pass a ref to the manager to load other assets

        RequestSceneLoad("scenes/coffee_cart/CoffeeCart_01_2k.gltf");
        LOG_INFO("Asset module started");

        LOG_INFO("----------------- READY! -----------------");

        RunMainLoop(platform, ui, renderer);

        ui.Shutdown();
        renderer.Shutdown();
        platform.Shutdown();
        PerformanceTelemetry::Shutdown();

        LOG_WARN("----------------- Shutting down, goodbye! -----------------");

        return 0;
    }

    void App::RunMainLoop(platform::Platform &platform,
                          ui::Ui &ui,
                          renderer::Renderer &renderer)
    {
        auto last_time = std::chrono::steady_clock::now();
        uint64_t frame_index = 0;
        double elapsed_seconds = 0.0;

        while (!m_should_quit && !platform.ShouldClose())
        {
            HYBRID_PROFILE_ZONE_N("App::Frame");
            bool active_scene_changed = false;
            {
                HYBRID_PROFILE_ZONE_N("App::PollEvents");
                platform.PollEvents();
            }
            {
                HYBRID_PROFILE_ZONE_N("App::ProcessPlatformEvents");
                ProcessPlatformEvents(platform.Events());
            }

            float delta_seconds = 0.0f;
            {
                HYBRID_PROFILE_ZONE_N("App::UpdateFrameTime");
                auto now = std::chrono::steady_clock::now();
                delta_seconds = std::chrono::duration<float>(now - last_time).count();
                last_time = now;
                elapsed_seconds += delta_seconds;
                frame_index++;
            }

            {
                HYBRID_PROFILE_ZONE_N("App::TryConsumeSceneLoad");
                if (scene::SceneLoadResult load_result{}; m_scene_loader.TryConsumeResult(load_result))
                {
                    if (load_result.success)
                    {
                        m_active_scene = load_result.scene_id;
                        active_scene_changed = true;
                        LOG_INFO("[App] Scene loaded: " + load_result.path);
                    }
                    else
                    {
                        LOG_ERROR("[App] Scene load failed: " + load_result.path);
                    }
                }
            }

            scene::SceneWorld *active_scene_world = nullptr;
            {
                HYBRID_PROFILE_ZONE_N("App::ResolveActiveSceneWorld");
                active_scene_world = m_active_scene.IsValid()
                                         ? m_assets.Get<scene::SceneWorld>(m_active_scene)
                                         : nullptr;
            }
            {
                HYBRID_PROFILE_ZONE_N("App::FlushSceneOnActivation");
                if (active_scene_world && active_scene_changed)
                {
                    active_scene_world->FlushPendingChanges();
                }
            }

            renderer::FrameContext frame_context{};
            {
                HYBRID_PROFILE_ZONE_N("App::BuildFrameContext");
                frame_context.frame_index = frame_index;
                frame_context.delta_seconds = delta_seconds;
                frame_context.time_seconds = elapsed_seconds;
                frame_context.render_extent = m_render_settings.render_extent;
            }

            renderer::RendererOutputs renderer_outputs{};
            renderer::RenderView resolved_view{};
            bool resolved_view_valid = false;
            if (renderer.BeginFrame(frame_context))
            {
                if (active_scene_world)
                {
                    const float aspect = static_cast<float>(std::max(m_render_settings.render_extent.width, 1u)) /
                                         static_cast<float>(std::max(m_render_settings.render_extent.height, 1u));
                    const scene::SceneCameraView scene_view =
                        scene::ResolvePrimaryCameraView(*active_scene_world, aspect);

                    renderer::RenderView view{};
                    if (scene_view.valid)
                    {
                        view.view = scene_view.view;
                        view.projection = scene_view.projection;
                        view.position = scene_view.position;
                        view.near_plane = scene_view.near_plane;
                        view.far_plane = scene_view.far_plane;
                        resolved_view_valid = true;
                    }
                    resolved_view = view;
                    renderer.SubmitScene(*active_scene_world, view, m_render_settings);
                }
                renderer_outputs = renderer.EndFrame();
            }

            const renderer::RendererStats &renderer_stats = renderer.GetStats();
            const double fps = delta_seconds > 1e-6f ? 1.0 / static_cast<double>(delta_seconds) : 0.0;
            FramePerformanceSample frame_sample{};
            frame_sample.time_seconds = elapsed_seconds;
            frame_sample.renderer_cpu_frame_ms = renderer_stats.cpu_frame_ms;
            frame_sample.fps = fps;
            frame_sample.draw_calls = renderer_stats.gbuffer.draw_calls;
            frame_sample.submitted_primitives = renderer_stats.submitted_primitives;
            frame_sample.submitted_vertices = renderer_stats.submitted_vertices;
            frame_sample.submitted_triangles = renderer_stats.submitted_triangles;
            frame_sample.gbuffer_uniform_updates = renderer_stats.gbuffer.uniform_updates;
            frame_sample.gbuffer_texture_binds = renderer_stats.gbuffer.texture_binds;
            frame_sample.gbuffer_primitive_cache_misses = renderer_stats.gbuffer.primitive_cache_misses;
            frame_sample.gbuffer_texture_cache_misses = renderer_stats.gbuffer.texture_cache_misses;
            frame_sample.gbuffer_primitive_uploads = renderer_stats.gbuffer.primitive_uploads;
            frame_sample.gbuffer_texture_uploads = renderer_stats.gbuffer.texture_uploads;
            PerformanceTelemetry::RecordFrameSample(frame_sample);

            ui::UiState ui_state{};
            {
                HYBRID_PROFILE_ZONE_N("App::BuildUiState");
                ui_state.scene_world = active_scene_world;
                ui_state.viewport_color_texture = renderer_outputs.color;
                ui_state.viewport_color_channels.rgb = renderer_outputs.color_channels.rgb;
                ui_state.viewport_color_channels.r = renderer_outputs.color_channels.r;
                ui_state.viewport_color_channels.g = renderer_outputs.color_channels.g;
                ui_state.viewport_color_channels.b = renderer_outputs.color_channels.b;
                ui_state.viewport_color_channels.a = renderer_outputs.color_channels.a;
                ui_state.viewport_gbuffer_rt0_texture = renderer_outputs.gbuffer_rt0;
                ui_state.viewport_gbuffer_rt0_channels.rgb = renderer_outputs.gbuffer_rt0_channels.rgb;
                ui_state.viewport_gbuffer_rt0_channels.r = renderer_outputs.gbuffer_rt0_channels.r;
                ui_state.viewport_gbuffer_rt0_channels.g = renderer_outputs.gbuffer_rt0_channels.g;
                ui_state.viewport_gbuffer_rt0_channels.b = renderer_outputs.gbuffer_rt0_channels.b;
                ui_state.viewport_gbuffer_rt0_channels.a = renderer_outputs.gbuffer_rt0_channels.a;
                ui_state.viewport_gbuffer_rt1_texture = renderer_outputs.gbuffer_rt1;
                ui_state.viewport_gbuffer_rt1_channels.rgb = renderer_outputs.gbuffer_rt1_channels.rgb;
                ui_state.viewport_gbuffer_rt1_channels.r = renderer_outputs.gbuffer_rt1_channels.r;
                ui_state.viewport_gbuffer_rt1_channels.g = renderer_outputs.gbuffer_rt1_channels.g;
                ui_state.viewport_gbuffer_rt1_channels.b = renderer_outputs.gbuffer_rt1_channels.b;
                ui_state.viewport_gbuffer_rt1_channels.a = renderer_outputs.gbuffer_rt1_channels.a;
                ui_state.viewport_raytrace_heatmap_texture = renderer_outputs.raytrace_heatmap;
                ui_state.viewport_entity_id_texture = renderer_outputs.gbuffer_entity_id;
                ui_state.viewport_gbuffer_depth_texture = renderer_outputs.depth;
                ui_state.viewport_render_extent = frame_context.render_extent;
                ui_state.viewport_render_view = resolved_view;
                ui_state.viewport_render_view_valid = resolved_view_valid;
                ui_state.render_settings = &m_render_settings;
            }

            ui::CommandBuffer commands;
            {
                HYBRID_PROFILE_ZONE_N("App::UiFrame");
                commands = ui.Frame(delta_seconds, ui_state);
            }

            std::optional<std::string> requested_scene_path;
            {
                HYBRID_PROFILE_ZONE_N("App::ProcessUiCommands");
                ui::CommandBuffer mutation_commands;
                mutation_commands.reserve(commands.size());
                for (const ui::UiCommand &command : commands)
                {
                    if (const auto *scene_load_command = ui::GetCommandIf<ui::RequestSceneLoadCommand>(&command))
                    {
                        if (!scene_load_command->path.empty())
                        {
                            requested_scene_path = scene_load_command->path;
                        }
                        continue;
                    }

                    mutation_commands.push_back(command);
                }

                ProcessUiCommands(mutation_commands, m_assets, m_active_scene, m_should_quit);
                if (active_scene_world && !mutation_commands.empty())
                {
                    active_scene_world->FlushPendingChanges();
                }
            }
            {
                HYBRID_PROFILE_ZONE_N("App::HandleSceneLoadRequest");
                if (requested_scene_path.has_value())
                {
                    if (m_active_scene.IsValid())
                    {
                        const std::string previous_scene_path = m_assets.GetPath(m_active_scene);
                        if (!m_assets.Unload(m_active_scene))
                        {
                            LOG_WARN("[App] Failed to unload active scene before loading a new selection");
                        }
                        else if (!previous_scene_path.empty())
                        {
                            LOG_INFO("[App] Unloaded scene: " + previous_scene_path);
                        }
                        m_active_scene = {};
                    }

                    RequestSceneLoad(*requested_scene_path);
                }
            }

            {
                HYBRID_PROFILE_ZONE_N("App::SwapBuffers");
                platform.SwapBuffers();
            }
            HYBRID_PROFILE_FRAME();
        }
    }

    void App::ProcessPlatformEvents(const platform::PlatformEvents &events)
    {
        for (const auto &event : events)
        {
            if (event.type == platform::PlatformEvent::Type::WindowClose)
            {
                m_should_quit = true;
            }
        }
    }

    void App::RequestSceneLoad(const std::string &path)
    {
        LOG_INFO("[App] Queuing scene load: " + path);
        m_scene_loader.RequestLoad(path);
    }

} // namespace hybrid::core
