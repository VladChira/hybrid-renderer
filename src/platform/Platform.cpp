#include "platform/Platform.h"

#include "core/Log.h"

#include <GLFW/glfw3.h>

#include <cstddef>
#include <utility>

namespace hybrid::platform
{

    namespace
    {

        void ErrorCallback(int error, const char *description)
        {
            (void)error;
            (void)description;
        }

        Platform *GetPlatform(GLFWwindow *window)
        {
            return static_cast<Platform *>(glfwGetWindowUserPointer(window));
        }

        void WindowCloseCallback(GLFWwindow *window)
        {
            if (auto *platform = GetPlatform(window))
            {
                platform->HandleWindowClose();
            }
        }

        void WindowSizeCallback(GLFWwindow *window, int width, int height)
        {
            if (auto *platform = GetPlatform(window))
            {
                platform->HandleWindowSize(width, height);
            }
        }

        void FramebufferSizeCallback(GLFWwindow *window, int width, int height)
        {
            if (auto *platform = GetPlatform(window))
            {
                platform->HandleFramebufferSize(width, height);
            }
        }

        void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
        {
            if (auto *platform = GetPlatform(window))
            {
                platform->HandleKey(key, scancode, action, mods);
            }
        }

        void MouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
        {
            if (auto *platform = GetPlatform(window))
            {
                platform->HandleMouseButton(button, action, mods);
            }
        }

        void CursorPosCallback(GLFWwindow *window, double x, double y)
        {
            if (auto *platform = GetPlatform(window))
            {
                platform->HandleCursorPos(x, y);
            }
        }

        void ScrollCallback(GLFWwindow *window, double x, double y)
        {
            if (auto *platform = GetPlatform(window))
            {
                platform->HandleScroll(x, y);
            }
        }

        void DropCallback(GLFWwindow *window, int count, const char **paths)
        {
            if (auto *platform = GetPlatform(window))
            {
                platform->HandleDrop(count, paths);
            }
        }

    } // namespace

    bool Platform::Init(const PlatformConfig &config)
    {
        if (m_initialized)
        {
            return true;
        }

        LOG_INFO("Initializing GLFW...");
        glfwSetErrorCallback(ErrorCallback);
        if (!glfwInit())
        {
            LOG_ERROR("GLFW failed to initialize");
            return false;
        }

#if defined(HYBRID_RHI_VULKAN)
        // Vulkan path: create a window without an OpenGL context. The
        // VulkanRenderBackend creates the surface from this window later.
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

        GLFWwindow *window = glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
        if (!window)
        {
            glfwTerminate();
            return false;
        }

        m_window = window;
        glfwSetWindowUserPointer(window, this);

        glfwSetWindowCloseCallback(window, WindowCloseCallback);
        glfwSetWindowSizeCallback(window, WindowSizeCallback);
        glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
        glfwSetKeyCallback(window, KeyCallback);
        glfwSetMouseButtonCallback(window, MouseButtonCallback);
        glfwSetCursorPosCallback(window, CursorPosCallback);
        glfwSetScrollCallback(window, ScrollCallback);
        glfwSetDropCallback(window, DropCallback);

#if defined(HYBRID_RHI_VULKAN)
        LOG_INFO("GLFW initialized for Vulkan (no GL context)");
#else
        glfwMakeContextCurrent(window);
        glfwSwapInterval(config.vsync ? 1 : 0);

        const int gl_major = glfwGetWindowAttrib(window, GLFW_CONTEXT_VERSION_MAJOR);
        const int gl_minor = glfwGetWindowAttrib(window, GLFW_CONTEXT_VERSION_MINOR);
        if (gl_major < 4 || (gl_major == 4 && gl_minor < 6))
        {
            LOG_ERROR("OpenGL 4.6 required, but created context is {}.{}", gl_major, gl_minor);
            glfwDestroyWindow(window);
            m_window = nullptr;
            glfwTerminate();
            return false;
        }

        LOG_INFO("GLFW initialized with OpenGL {}.{}", gl_major, gl_minor);
#endif

        m_initialized = true;
        return true;
    }

    void Platform::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }

        LOG_WARN("Platform module shutting down...");

        if (m_window)
        {
            glfwDestroyWindow(static_cast<GLFWwindow *>(m_window));
            m_window = nullptr;
        }

        glfwTerminate();
        m_initialized = false;
    }

    void Platform::PollEvents()
    {
        m_events.clear();
        m_input.scroll_x = 0.0;
        m_input.scroll_y = 0.0;

        glfwPollEvents();
    }

    void Platform::SwapBuffers()
    {
        if (!m_window)
        {
            return;
        }
        glfwSwapBuffers(static_cast<GLFWwindow *>(m_window));
    }

    bool Platform::ShouldClose() const
    {
        if (m_should_close)
        {
            return true;
        }
        if (!m_window)
        {
            return true;
        }
        return glfwWindowShouldClose(static_cast<GLFWwindow *>(m_window)) == GLFW_TRUE;
    }

    void Platform::RequestClose()
    {
        m_should_close = true;
        if (m_window)
        {
            glfwSetWindowShouldClose(static_cast<GLFWwindow *>(m_window), GLFW_TRUE);
        }
    }

    NativeWindowHandle Platform::GetNativeHandle() const
    {
        return NativeWindowHandle{m_window};
    }

    const PlatformEvents &Platform::Events() const
    {
        return m_events;
    }

    const InputState &Platform::Input() const
    {
        return m_input;
    }

    void Platform::HandleWindowClose()
    {
        PlatformEvent event{};
        event.type = PlatformEvent::Type::WindowClose;
        m_events.push_back(std::move(event));
        m_should_close = true;
    }

    void Platform::HandleWindowSize(int width, int height)
    {
        PlatformEvent event{};
        event.type = PlatformEvent::Type::WindowResize;
        event.width = width;
        event.height = height;
        m_events.push_back(std::move(event));
    }

    void Platform::HandleFramebufferSize(int width, int height)
    {
        PlatformEvent event{};
        event.type = PlatformEvent::Type::FramebufferResize;
        event.width = width;
        event.height = height;
        m_events.push_back(std::move(event));
    }

    void Platform::HandleKey(int key, int scancode, int action, int mods)
    {
        if (key >= 0 && key < InputState::kMaxKeys)
        {
            m_input.keys[static_cast<size_t>(key)] = static_cast<uint8_t>(action != GLFW_RELEASE);
        }

        PlatformEvent event{};
        event.type = PlatformEvent::Type::Key;
        event.key = key;
        event.scancode = scancode;
        event.action = action;
        event.mods = mods;
        m_events.push_back(std::move(event));
    }

    void Platform::HandleMouseButton(int button, int action, int mods)
    {
        if (button >= 0 && button < InputState::kMaxMouseButtons)
        {
            m_input.mouse_buttons[static_cast<size_t>(button)] = static_cast<uint8_t>(action != GLFW_RELEASE);
        }

        PlatformEvent event{};
        event.type = PlatformEvent::Type::MouseButton;
        event.button = button;
        event.action = action;
        event.mods = mods;
        m_events.push_back(std::move(event));
    }

    void Platform::HandleCursorPos(double x, double y)
    {
        m_input.mouse_x = x;
        m_input.mouse_y = y;

        PlatformEvent event{};
        event.type = PlatformEvent::Type::CursorPos;
        event.x = x;
        event.y = y;
        m_events.push_back(std::move(event));
    }

    void Platform::HandleScroll(double x, double y)
    {
        m_input.scroll_x += x;
        m_input.scroll_y += y;

        PlatformEvent event{};
        event.type = PlatformEvent::Type::Scroll;
        event.scroll_x = x;
        event.scroll_y = y;
        m_events.push_back(std::move(event));
    }

    void Platform::HandleDrop(int count, const char **paths)
    {
        for (int i = 0; i < count; ++i)
        {
            PlatformEvent event{};
            event.type = PlatformEvent::Type::DropFile;
            if (paths && paths[i])
            {
                event.path = paths[i];
            }
            m_events.push_back(std::move(event));
        }
    }

} // namespace hybrid::platform
