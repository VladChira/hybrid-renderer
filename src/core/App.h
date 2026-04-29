#pragma once

#include "assets/AssetManager.h"
#include "core/scene/SceneLoadService.h"
#include "platform/Platform.h"
#include "renderer/Renderer.h"
#include "ui/Ui.h"

#include <cstdint>
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
        void RequestSceneLoad(const std::string &path);

        assets::AssetManager m_assets;
        scene::SceneLoadService m_scene_loader;
        assets::AssetId m_active_scene{};
        renderer::RenderSettings m_render_settings{};
        bool m_should_quit = false;

        // Vulkan offscreen-as-ImTextureID lifecycle. Zero / nullptr in GL
        // builds. The view handle is cached (as void*) so we can detect
        // when the renderer recreates the offscreen image on resize and
        // re-register with ImGui. uint64_t is the storage type of
        // UiState::viewport_color_texture.
        uint64_t m_vk_viewport_texture = 0;
        void *m_vk_offscreen_view_cache = nullptr;
        // Phase 3-stage-B: gbuffer RT1 (normal+roughness). Registered with
        // ImGui alongside the offscreen / RT0 so the RenderTargetsPanel can
        // display both. Re-registered on resize via the same view-handle
        // poll as RT0.
        uint64_t m_vk_rt1_texture = 0;
        void *m_vk_rt1_view_cache = nullptr;
    };

} // namespace hybrid::core
