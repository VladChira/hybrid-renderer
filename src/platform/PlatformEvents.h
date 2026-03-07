#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hybrid::platform
{

  struct InputState
  {
    static constexpr int kMaxKeys = 512;
    static constexpr int kMaxMouseButtons = 16;

    std::array<uint8_t, kMaxKeys> keys{};
    std::array<uint8_t, kMaxMouseButtons> mouse_buttons{};

    double mouse_x = 0.0;
    double mouse_y = 0.0;
    double scroll_x = 0.0;
    double scroll_y = 0.0;
  };

  struct PlatformEvent
  {
    enum class Type
    {
      WindowClose,
      WindowResize,
      FramebufferResize,
      Key,
      MouseButton,
      CursorPos,
      Scroll,
      DropFile
    };

    Type type = Type::WindowClose;

    int width = 0;
    int height = 0;

    int key = 0;
    int scancode = 0;
    int action = 0;
    int mods = 0;

    int button = 0;

    double x = 0.0;
    double y = 0.0;
    double scroll_x = 0.0;
    double scroll_y = 0.0;

    std::string path;
  };

  using PlatformEvents = std::vector<PlatformEvent>;

  struct NativeWindowHandle
  {
    void *window = nullptr;
  };

} // namespace hybrid::platform
