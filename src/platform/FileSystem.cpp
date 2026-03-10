#include "platform/FileSystem.h"

#include <fstream>

namespace hybrid::platform
{

    bool FileSystem::ReadAllBytes(const std::string &path, std::vector<std::byte> &out_bytes)
    {
        out_bytes.clear();

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            return false;
        }

        const std::streamsize size = file.tellg();
        if (size < 0)
        {
            return false;
        }

        out_bytes.resize(static_cast<size_t>(size));
        if (size == 0)
        {
            return true;
        }

        file.seekg(0, std::ios::beg);
        return static_cast<bool>(file.read(reinterpret_cast<char *>(out_bytes.data()), size));
    }

} // namespace hybrid::platform
