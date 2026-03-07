#pragma once

#include "platform/PlatformEvents.h"

#include <string>

namespace hybrid::platform
{

  struct PlatformConfig
  {
    int width = 1280;
    int height = 720;
    std::string title = "Hybrid Renderer";
    bool vsync = true;
  };

  class Platform
  {
  public:
    bool Init(const PlatformConfig &config);
    void Shutdown();

    void PollEvents();
    void SwapBuffers();

    bool ShouldClose() const;
    void RequestClose();

    NativeWindowHandle GetNativeHandle() const;
    const PlatformEvents &Events() const;
    const InputState &Input() const;

    void HandleWindowClose();
    void HandleWindowSize(int width, int height);
    void HandleFramebufferSize(int width, int height);
    void HandleKey(int key, int scancode, int action, int mods);
    void HandleMouseButton(int button, int action, int mods);
    void HandleCursorPos(double x, double y);
    void HandleScroll(double x, double y);
    void HandleDrop(int count, const char **paths);

  private:
    void *m_window = nullptr;
    bool m_initialized = false;
    bool m_should_close = false;

    PlatformEvents m_events;
    InputState m_input;
  };

} // namespace hybrid::platform
