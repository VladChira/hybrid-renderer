#pragma once

#include "assets/AssetManager.h"

#include <string>

namespace hybrid::assets
{

    class DiskAssetDataSource final : public IAssetDataSource
    {
    public:
        static std::string DefaultRootPath();

        explicit DiskAssetDataSource(std::string root_path = DefaultRootPath());

        bool ReadAllBytes(const std::string &path, std::vector<std::byte> &out_bytes) override;

        const std::string &RootPath() const { return m_root_path; }

    private:
        std::string ResolvePath(const std::string &path) const;

        std::string m_root_path;
    };

} // namespace hybrid::assets
