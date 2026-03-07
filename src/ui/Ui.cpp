#include "ui/Ui.h"

#include <glad.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
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

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

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

        m_window = window;
        m_config = config;
        m_initialized = true;
        return true;
    }

    void Ui::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }

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

        ImGui::Begin("Hybrid Renderer");
        float fps = delta_seconds > 0.0f ? (1.0f / delta_seconds) : 0.0f;
        ImGui::Text("FPS: %.1f", fps);
        if (ImGui::Button("Quit"))
        {
            commands.push_back(UiCommand{UiCommand::Type::Quit});
        }
        ImGui::End();

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

} // namespace hybrid::ui
