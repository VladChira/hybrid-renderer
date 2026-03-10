#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace hybrid::platform
{

    class FileSystem
    {
    public:
        static bool ReadAllBytes(const std::string &path, std::vector<std::byte> &out_bytes);
    };

} // namespace hybrid::platform
