#pragma once

#include "platform/PlatformEvents.h"
#include "ui/UiCommands.h"

#include <string>

namespace hybrid::ui
{

    struct UiConfig
    {
        std::string glsl_version = "#version 330";
    };

    class Ui
    {
    public:
        bool Init(const UiConfig &config, const platform::NativeWindowHandle &window_handle);
        void Shutdown();

        CommandBuffer Frame(float delta_seconds);

    private:
        void *m_window = nullptr;
        bool m_initialized = false;
        UiConfig m_config{};
    };

} // namespace hybrid::ui
