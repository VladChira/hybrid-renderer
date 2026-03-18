#include "graphics/GraphicsRuntime.h"

#include "core/Log.h"

#include <mutex>

namespace hybrid::graphics
{

    namespace
    {
        std::mutex g_runtime_mutex;
        GraphicsBackend g_active_backend = GraphicsBackend::None;
        bool g_opengl_initialized = false;
    } // namespace

    bool EnsureOpenGLInitialized(GLADloadproc load_proc)
    {
        std::scoped_lock lock(g_runtime_mutex);

        if (g_active_backend == GraphicsBackend::OpenGL && g_opengl_initialized)
        {
            return true;
        }

        if (g_active_backend != GraphicsBackend::None &&
            g_active_backend != GraphicsBackend::OpenGL)
        {
            LOG_ERROR("[GraphicsRuntime] Cannot initialize OpenGL while another graphics backend is active");
            return false;
        }

        if (load_proc == nullptr)
        {
            LOG_ERROR("[GraphicsRuntime] OpenGL init failed: null GL loader callback");
            return false;
        }

        if (!gladLoadGLLoader(load_proc))
        {
            LOG_ERROR("[GraphicsRuntime] OpenGL init failed: gladLoadGLLoader returned false");
            return false;
        }

        g_active_backend = GraphicsBackend::OpenGL;
        g_opengl_initialized = true;
        LOG_INFO("[GraphicsRuntime] OpenGL initialized");
        return true;
    }

    GraphicsBackend ActiveBackend()
    {
        std::scoped_lock lock(g_runtime_mutex);
        return g_active_backend;
    }

} // namespace hybrid::graphics

