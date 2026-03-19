#pragma once

#include "assets/AssetManager.h"
#include "core/scene/SceneLoadService.h"
#include "platform/Platform.h"
#include "renderer/Renderer.h"
#include "ui/Ui.h"

#include <string>

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
        App();
        int Run(const AppConfig &config = {});

    private:
        void RunMainLoop(platform::Platform &platform,
                         ui::Ui &ui,
                         renderer::Renderer &renderer);
        void ProcessPlatformEvents(const platform::PlatformEvents &events);
        void ProcessUiCommands(const ui::CommandBuffer &commands);
        void RequestSceneLoad(const std::string &path);

        assets::AssetManager m_assets;
        scene::SceneLoadService m_scene_loader;
        assets::AssetId m_active_scene{};
        bool m_should_quit = false;
    };

} // namespace hybrid::core
