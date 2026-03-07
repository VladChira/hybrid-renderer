#include "ui/Ui.h"

#include "core/Log.h"
#include "themes/Themes.h"

#include <glad.h>

#include <utility>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

namespace hybrid::ui
{

    namespace
    {
        class PlaceholderPanel final : public Panel
        {
        public:
            explicit PlaceholderPanel(std::string title)
                : Panel(std::move(title))
            {
            }

        private:
            void DrawContents(PanelContext &context) override
            {
                (void)context;
            }
        };
    }

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

        LOG_INFO("Initializing ImGui...");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        io = &ImGui::GetIO();
        io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        std::string font_path = std::string(HYBRID_PROJECT_ROOT) + "/assets/fonts/DMSans-Regular.ttf";
        io->Fonts->AddFontFromFileTTF(font_path.c_str(), 18.0);

        // Default theme
        embraceTheDarknessTheme();
        // embraceTheLightnessTheme();
        

        if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
        {
            ImGui::DestroyContext();
            return false;
        }

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init(config.glsl_version.c_str()))
        {
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            return false;
        }

        LOG_INFO("ImGui initialized");

        m_window = window;
        m_config = config;
        m_layout = DockspaceLayout::Default();
        if (m_panels.Empty())
        {
            RegisterPanel(std::make_unique<PlaceholderPanel>("Scene Hierarchy"));
            RegisterPanel(std::make_unique<PlaceholderPanel>("Properties"));
            RegisterPanel(std::make_unique<PlaceholderPanel>("Materials"));
            RegisterPanel(std::make_unique<PlaceholderPanel>("Content Browser"));
            RegisterPanel(std::make_unique<PlaceholderPanel>("Console"));
            RegisterPanel(std::make_unique<PlaceholderPanel>("Settings"));
            RegisterPanel(std::make_unique<PlaceholderPanel>("Viewport"));
            RegisterPanel(std::make_unique<PlaceholderPanel>("Performance"));
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
        ImGui::DestroyContext();

        m_window = nullptr;
        m_initialized = false;
    }

    CommandBuffer Ui::Frame(float delta_seconds)
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

        PanelContext context{};
        context.delta_seconds = delta_seconds;
        context.commands = &commands;
        m_panels.DrawAll(context);

        ImGui::Render();

        int fb_width = 0;
        int fb_height = 0;
        glfwGetFramebufferSize(static_cast<GLFWwindow *>(m_window), &fb_width, &fb_height);

        glViewport(0, 0, fb_width, fb_height);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        return commands;
    }

    void Ui::RegisterPanel(std::unique_ptr<Panel> panel)
    {
        m_panels.Register(std::move(panel));
        m_dockspace.ResetLayout();
    }

    void Ui::ClearPanels()
    {
        m_panels.Clear();
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
