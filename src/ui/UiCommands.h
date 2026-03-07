#pragma once

#include <vector>

namespace hybrid::ui
{

    struct UiCommand
    {
        enum class Type
        {
            Quit
        };

        Type type = Type::Quit;
    };

    using CommandBuffer = std::vector<UiCommand>;

} // namespace hybrid::ui
