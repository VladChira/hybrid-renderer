#include "ui/Ui.h"

#include "core/Log.h"
#include "core/ResourceMonitor.h"
#include "graphics/GraphicsRuntime.h"
#include "themes/Themes.h"
#include "panels/ConsolePanel.h"
#include "panels/MaterialsPanel.h"
#include "panels/PlaceholderPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/PerformancePanel.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/SettingsPanel.h"
#include "panels/ToolbarPanel.h"
#include "panels/ViewportPanel.h"

#include <glad.h>

#include <utility>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <implot.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

namespace hybrid::ui
{

    bool Ui::Init(const UiConfig &config, const platform::NativeWindowHandle &window_handle)
    {
        if (m_initialized)
        {
            return true;
        }

        auto *window = static_cast<GLFWwindow *>(window_handle.window);
        if (!window)
        {
            return false;
        }

        if (glfwGetCurrentContext() == nullptr)
        {
            LOG_ERROR("UI init failed: no current OpenGL context");
            return false;
        }

        if (!graphics::EnsureOpenGLInitialized(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            LOG_ERROR("UI init failed: OpenGL runtime initialization failed");
            return false;
        }

        LOG_INFO("Initializing ImGui...");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();

        io = &ImGui::GetIO();
        io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        std::string font_path = std::string(HYBRID_PROJECT_ROOT) + "/assets/fonts/DMSans-Regular.ttf";
        io->Fonts->AddFontFromFileTTF(font_path.c_str(), 18.0);

        LOG_INFO("Default theme is " + ThemeKindToString(config.theme));
        ApplyTheme(config.theme);
        

        if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
        {
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init(config.glsl_version.c_str()))
        {
            ImGui_ImplGlfw_Shutdown();
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
            return false;
        }

        LOG_INFO("ImGui initialized");

        m_window = window;
        m_config = config;
        m_layout = DockspaceLayout::Default();
        m_theme = config.theme;
        m_theme_palette = BuildThemePalette(m_theme);
        core::ResourceMonitor::Init();
        if (m_panels.Empty())
        {
            RegisterPanel(std::make_unique<ToolbarPanel>(), DockTarget::Top);
            RegisterPanel(std::make_unique<SceneHierarchyPanel>(), DockTarget::RightTop);
            RegisterPanel(std::make_unique<PropertiesPanel>(), DockTarget::RightBottom);
            RegisterPanel(std::make_unique<MaterialsPanel>(), DockTarget::BottomLeft);
            RegisterPanel(std::make_unique<PlaceholderPanel>("Content Browser"), DockTarget::RightTop);
            RegisterPanel(std::make_unique<ConsolePanel>(), DockTarget::BottomRight);
            RegisterPanel(std::make_unique<SettingsPanel>(), DockTarget::LeftTop);
            RegisterPanel(std::make_unique<ViewportPanel>(), DockTarget::Main);
            RegisterPanel(std::make_unique<PerformancePanel>(), DockTarget::LeftBottom);
        }
        m_initialized = true;
        return true;
    }

    void Ui::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }

        LOG_WARN("UI module shutting down...");

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        core::ResourceMonitor::Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();

        m_window = nullptr;
        m_initialized = false;
    }

    CommandBuffer Ui::Frame(float delta_seconds, const UiState &state)
    {
        CommandBuffer commands;
        if (!m_initialized || !m_window)
        {
            return commands;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        m_dockspace.BeginFrame();
        m_dockspace.BuildLayout(m_layout);

        UiState frame_state = state;
        frame_state.viewport_visualization = m_viewport_visualization;

        PanelContext context{};
        context.delta_seconds = delta_seconds;
        context.commands = &commands;
        context.theme = &m_theme_palette;
        context.state = &frame_state;
        context.selection = &m_selection;
        context.viewport_visualization = &m_viewport_visualization;
        m_panels.DrawAll(context);

        ImGui::Render();

        int fb_width = 0;
        int fb_height = 0;
        glfwGetFramebufferSize(static_cast<GLFWwindow *>(m_window), &fb_width, &fb_height);

        glViewport(0, 0, fb_width, fb_height);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        return commands;
    }

    void Ui::RegisterPanel(std::unique_ptr<Panel> panel, DockTarget target)
    {
        const std::string title = panel->Title();
        LOG_INFO("Registering panel " + title);
        m_panels.Register(std::move(panel));
        for (auto it = m_layout.assignments.begin(); it != m_layout.assignments.end(); ++it)
        {
            if (it->panel_title == title)
            {
                m_layout.assignments.erase(it);
                break;
            }
        }
        m_layout.assignments.push_back({title, target});
        m_dockspace.ResetLayout();
    }

    void Ui::ClearPanels()
    {
        m_panels.Clear();
        m_layout.assignments.clear();
        m_dockspace.ResetLayout();
    }

    void Ui::SetDockspaceLayout(const DockspaceLayout &layout)
    {
        m_layout = layout;
        m_dockspace.ResetLayout();
    }

    void Ui::ResetDockspaceLayout()
    {
        m_dockspace.ResetLayout();
    }

} // namespace hybrid::ui
