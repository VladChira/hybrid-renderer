#pragma once

#include "assets/AssetManager.h"
#include "assets/ImageAsset.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace hybrid::assets
{

    class AssimpTextureCache final
    {
    public:
        AssimpTextureCache(AssetManager *asset_manager, std::string scene_path);

        AssetHandle<ImageAsset> GetOrLoad(const std::string &texture_path);

    private:
        std::string ResolveTexturePath(const std::string &texture_path) const;

        AssetManager *m_assets = nullptr;
        std::string m_scene_path;
        std::unordered_map<std::string, AssetHandle<ImageAsset>> m_cache;
        std::mutex m_mutex;
    };

} // namespace hybrid::assets

