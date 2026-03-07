#include "core/App.h"

#include <chrono>

namespace hybrid::core
{
    /**
     * Initialize the modules, run the main loop, shutdown in reverse order
     */
    int App::Run(const AppConfig &config)
    {
        platform::Platform platform;
        if (!platform.Init(config.platform))
        {
            return 1;
        }

        ui::Ui ui;
        if (!ui.Init(config.ui, platform.GetNativeHandle()))
        {
            platform.Shutdown();
            return 2;
        }

        RunMainLoop(platform, ui);

        ui.Shutdown();
        platform.Shutdown();

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

} // namespace hybrid::core
