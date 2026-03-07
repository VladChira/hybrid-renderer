#pragma once

#include "platform/Platform.h"
#include "ui/Ui.h"

namespace hybrid::core
{

    struct AppConfig
    {
        platform::PlatformConfig platform{};
        ui::UiConfig ui{};
    };

    class App
    {
    public:
        int Run(const AppConfig &config = {});

    private:
        void RunMainLoop(platform::Platform &platform, ui::Ui &ui);
        void ProcessPlatformEvents(const platform::PlatformEvents &events);
        void ProcessUiCommands(const ui::CommandBuffer &commands);

        bool m_should_quit = false;
    };

} // namespace hybrid::core
