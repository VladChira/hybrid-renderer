#include "assets/DiskAssetDataSource.h"

#include "platform/FileSystem.h"

#include <filesystem>

namespace hybrid::assets
{

    std::string DiskAssetDataSource::DefaultRootPath()
    {
#ifdef HYBRID_PROJECT_ROOT
        return std::string(HYBRID_PROJECT_ROOT) + "/assets";
#else
        return "assets";
#endif
    }

    DiskAssetDataSource::DiskAssetDataSource(std::string root_path)
        : m_root_path(std::move(root_path))
    {
    }

    bool DiskAssetDataSource::ReadAllBytes(const std::string &path, std::vector<std::byte> &out_bytes)
    {
        const std::string resolved = ResolvePath(path);
        return platform::FileSystem::ReadAllBytes(resolved, out_bytes);
    }

    std::string DiskAssetDataSource::ResolvePath(const std::string &path) const
    {
        if (m_root_path.empty())
        {
            return path;
        }

        std::filesystem::path resolved(path);
        if (resolved.is_absolute())
        {
            return resolved.make_preferred().string();
        }

        std::filesystem::path root(m_root_path);
        return (root / resolved).make_preferred().string();
    }

} // namespace hybrid::assets
