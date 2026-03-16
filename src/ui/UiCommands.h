#pragma once

#include <vector>

namespace hybrid::ui
{

    struct ViewportExtent
    {
        int width = 1;
        int height = 1;
    };

    struct UiCommand
    {
        enum class Type
        {
            Quit,
            ViewportResize
        };

        Type type = Type::Quit;
        ViewportExtent viewport_extent{};
    };

    using CommandBuffer = std::vector<UiCommand>;

} // namespace hybrid::ui
