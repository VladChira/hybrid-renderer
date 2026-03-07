#include "utils/PathUtils.h"

namespace hybrid::utils
{

    std::string ExtractExtension(std::string_view path)
    {
        const auto last_slash = path.find_last_of("/\\");
        const auto last_dot = path.find_last_of('.');
        if (last_dot == std::string_view::npos)
        {
            return {};
        }
        if (last_slash != std::string_view::npos && last_dot < last_slash)
        {
            return {};
        }
        return std::string(path.substr(last_dot + 1));
    }

} // namespace hybrid::utils
