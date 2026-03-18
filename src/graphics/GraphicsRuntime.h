#pragma once

#include <glad.h>

namespace hybrid::graphics
{

    enum class GraphicsBackend
    {
        None,
        OpenGL
    };

    bool EnsureOpenGLInitialized(GLADloadproc load_proc);
    GraphicsBackend ActiveBackend();

} // namespace hybrid::graphics

