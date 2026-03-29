#include "core/App.h"

#include <chrono>

#include "assets/AssimpSceneLoader.h"
#include "assets/DiskAssetDataSource.h"
#include "assets/StbImageLoader.h"

#include "core/scene/SceneWorld.h"
#include "core/scene/SceneCameraResolver.h"
#include "core/Log.h"
#include "core/UiCommandProcessor.h"
#include "ui/UiStateUtils.h"

#include "utils/Banner.h"

#include <algorithm>
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

        LOG_INFO("Starting Asset Manager...");
        m_assets.SetDataSource(std::make_shared<assets::DiskAssetDataSource>());
        m_assets.RegisterLoader(std::make_unique<assets::StbImageLoader>());
        m_assets.RegisterLoader(std::make_unique<assets::AssimpSceneLoader>(&m_assets)); // pass a ref to the manager to load other assets

        RequestSceneLoad("scenes/sponza_low/Sponza.gltf");
        LOG_INFO("Asset module started");

        LOG_INFO("----------------- READY! -----------------");

        RunMainLoop(platform, ui, renderer);

        ui.Shutdown();
        renderer.Shutdown();
        platform.Shutdown();

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
            platform.PollEvents();
            ProcessPlatformEvents(platform.Events());

            auto now = std::chrono::steady_clock::now();
            float delta_seconds = std::chrono::duration<float>(now - last_time).count();
            last_time = now;
            elapsed_seconds += delta_seconds;
            frame_index++;

            if (scene::SceneLoadResult load_result{}; m_scene_loader.TryConsumeResult(load_result))
            {
                if (load_result.success)
                {
                    m_active_scene = load_result.scene_id;
                    LOG_INFO("[App] Scene loaded: " + load_result.path);
                }
                else
                {
                    LOG_ERROR("[App] Scene load failed: " + load_result.path);
                }
            }

            scene::SceneWorld *active_scene_world = m_active_scene.IsValid()
                                                        ? m_assets.Get<scene::SceneWorld>(m_active_scene)
                                                        : nullptr;
            if (active_scene_world)
            {
                active_scene_world->UpdateTransforms();
            }

            renderer::FrameContext frame_context{};
            frame_context.frame_index = frame_index;
            frame_context.delta_seconds = delta_seconds;
            frame_context.time_seconds = elapsed_seconds;
            frame_context.render_extent = m_render_settings.render_extent;

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

            ui::UiState ui_state{};
            ui_state.scene_world = active_scene_world;
            ui::BuildMaterialEntries(active_scene_world, ui_state.materials);
            ui_state.viewport_color_texture = renderer_outputs.color;
            ui_state.viewport_gbuffer_rt0_texture = renderer_outputs.gbuffer_rt0;
            ui_state.viewport_gbuffer_rt1_texture = renderer_outputs.gbuffer_rt1;
            ui_state.viewport_entity_id_texture = renderer_outputs.gbuffer_entity_id;
            ui_state.viewport_render_extent = frame_context.render_extent;
            ui_state.viewport_render_view = resolved_view;
            ui_state.viewport_render_view_valid = resolved_view_valid;
            ui_state.render_settings = &m_render_settings;

            ui::CommandBuffer commands = ui.Frame(delta_seconds, ui_state);
            ProcessUiCommands(commands, m_assets, m_active_scene, m_should_quit);

            platform.SwapBuffers();
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
