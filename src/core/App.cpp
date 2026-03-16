#include "core/App.h"

#include <chrono>

#include "assets/AssimpSceneLoader.h"
#include "assets/DiskAssetDataSource.h"
#include "assets/StbImageLoader.h"

#include "core/scene/SceneWorld.h"
#include "core/Log.h"

#include "renderer/SceneWorldSnapshot.h"
#include "renderer/RendererUtils.h"

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
        renderer::RendererConfig renderer_config{};
        renderer_config.window = platform.GetNativeHandle();
        renderer_config.initial_extent = renderer::utils::ToRenderExtent(config.platform.width, config.platform.height);
        renderer_config.vsync = config.platform.vsync;
        if (!renderer.Init(renderer_config))
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

        RequestSceneLoad("scenes/sponza/Sponza.gltf");
        LOG_INFO("Asset module started");

        LOG_INFO("----------------- READY! -----------------");

        RunMainLoop(platform, ui, renderer, renderer_config.initial_extent);

        ui.Shutdown();
        renderer.Shutdown();
        platform.Shutdown();

        LOG_WARN("----------------- Shutting down, goodbye! -----------------");

        return 0;
    }

    void App::RunMainLoop(platform::Platform &platform,
                          ui::Ui &ui,
                          renderer::Renderer &renderer,
                          renderer::RenderExtent initial_render_extent)
    {
        auto last_time = std::chrono::steady_clock::now();
        renderer::RenderExtent current_render_extent = initial_render_extent;
        uint64_t frame_index = 0;
        double elapsed_seconds = 0.0;

        while (!m_should_quit && !platform.ShouldClose())
        {
            platform.PollEvents();
            ProcessPlatformEvents(platform.Events(), renderer, current_render_extent);

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
            frame_context.render_extent = current_render_extent;

            if (renderer.BeginFrame(frame_context))
            {
                if (active_scene_world)
                {
                    renderer::RenderView view{};
                    renderer::RenderSettings settings{};
                    renderer.SubmitScene(*active_scene_world, view, settings);
                }
                renderer.EndFrame();
            }

            ui::UiState ui_state{};
            ui_state.scene_world = active_scene_world;

            ui::CommandBuffer commands = ui.Frame(delta_seconds, ui_state);
            ProcessUiCommands(commands, renderer, current_render_extent);

            platform.SwapBuffers();
        }
    }

    void App::ProcessPlatformEvents(const platform::PlatformEvents &events,
                                    renderer::Renderer &renderer,
                                    renderer::RenderExtent &current_render_extent)
    {
        for (const auto &event : events)
        {
            if (event.type == platform::PlatformEvent::Type::WindowClose)
            {
                m_should_quit = true;
            }
            else if (event.type == platform::PlatformEvent::Type::FramebufferResize)
            {
                current_render_extent = renderer::utils::ToRenderExtent(event.width, event.height);
                renderer.Resize(current_render_extent);
            }
        }
    }

    void App::ProcessUiCommands(const ui::CommandBuffer &commands,
                                renderer::Renderer &renderer,
                                renderer::RenderExtent &current_render_extent)
    {
        for (const auto &command : commands)
        {
            if (command.type == ui::UiCommand::Type::Quit)
            {
                m_should_quit = true;
            }
            else if (command.type == ui::UiCommand::Type::ViewportResize)
            {
                current_render_extent = renderer::utils::ToRenderExtent(command.viewport_extent.width, command.viewport_extent.height);
                renderer.Resize(current_render_extent);
            }
        }
    }

    void App::RequestSceneLoad(const std::string &path)
    {
        LOG_INFO("[App] Queuing scene load: " + path);
        m_scene_loader.RequestLoad(path);
    }

} // namespace hybrid::core
