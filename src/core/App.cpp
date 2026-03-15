#include "core/App.h"

#include <chrono>

#include "assets/AssimpSceneLoader.h"
#include "assets/DiskAssetDataSource.h"
#include "assets/StbImageLoader.h"
#include "core/scene/SceneWorld.h"
#include "core/Log.h"
#include "utils/Banner.h"

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

        LOG_INFO("Starting UI module...");
        ui::Ui ui;
        if (!ui.Init(config.ui, platform.GetNativeHandle()))
        {
            LOG_CRITICAL("UI module failed to start, aborting...");
            platform.Shutdown();
            return 2;
        }
        LOG_INFO("UI module started");

        LOG_INFO("Starting Asset Manager...");
        m_assets.SetDataSource(std::make_shared<assets::DiskAssetDataSource>());
        m_assets.RegisterLoader(std::make_unique<assets::StbImageLoader>());
        m_assets.RegisterLoader(std::make_unique<assets::AssimpSceneLoader>(&m_assets)); // pass a ref to the manager to load other assets

        RequestSceneLoad("scenes/sponza/Sponza.gltf");
        LOG_INFO("Asset module started");

        LOG_INFO("----------------- READY! -----------------");

        RunMainLoop(platform, ui);

        ui.Shutdown();
        platform.Shutdown();

        LOG_WARN("----------------- Shutting down, goodbye! -----------------");

        return 0;
    }

    void App::RunMainLoop(platform::Platform &platform, ui::Ui &ui)
    {
        auto last_time = std::chrono::steady_clock::now();

        while (!m_should_quit && !platform.ShouldClose())
        {
            platform.PollEvents();
            ProcessPlatformEvents(platform.Events());

            auto now = std::chrono::steady_clock::now();
            float delta_seconds = std::chrono::duration<float>(now - last_time).count();
            last_time = now;

            ui::CommandBuffer commands = ui.Frame(delta_seconds);
            ProcessUiCommands(commands);

            scene::SceneLoadResult load_result{};
            if (m_scene_loader.TryConsumeResult(load_result))
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

    void App::ProcessUiCommands(const ui::CommandBuffer &commands)
    {
        for (const auto &command : commands)
        {
            if (command.type == ui::UiCommand::Type::Quit)
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
