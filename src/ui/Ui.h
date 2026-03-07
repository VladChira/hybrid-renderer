#pragma once

#include "platform/PlatformEvents.h"
#include "ui/Dockspace.h"
#include "ui/panels/Panel.h"
#include "ui/UiCommands.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <memory>
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

        void RegisterPanel(std::unique_ptr<Panel> panel);
        void ClearPanels();
        void SetDockspaceLayout(const DockspaceLayout &layout);
        void ResetDockspaceLayout();

        CommandBuffer Frame(float delta_seconds);

    private:
        void *m_window = nullptr;
        bool m_initialized = false;
        UiConfig m_config{};
        DockspaceLayout m_layout{};
        Dockspace m_dockspace{};
        PanelRegistry m_panels{};

        ImGuiIO *io;
    };

} // namespace hybrid::ui
