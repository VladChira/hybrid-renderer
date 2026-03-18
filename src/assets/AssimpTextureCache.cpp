#include "assets/AssimpTextureCache.h"

#include <filesystem>
#include <utility>

namespace hybrid::assets
{

    AssimpTextureCache::AssimpTextureCache(AssetManager *asset_manager, std::string scene_path)
        : m_assets(asset_manager), m_scene_path(std::move(scene_path))
    {
    }

    AssetHandle<ImageAsset> AssimpTextureCache::GetOrLoad(const std::string &texture_path)
    {
        if (!m_assets || texture_path.empty())
        {
            return {};
        }

        const std::string resolved_path = ResolveTexturePath(texture_path);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_cache.find(resolved_path);
            if (it != m_cache.end())
            {
                return it->second;
            }
        }

        auto image_handle = m_assets->LoadHandle<ImageAsset>(resolved_path);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const auto [it, inserted] = m_cache.emplace(resolved_path, image_handle);
            (void)inserted;
            return it->second;
        }
    }

    std::string AssimpTextureCache::ResolveTexturePath(const std::string &texture_path) const
    {
        std::filesystem::path tex(texture_path);
        if (tex.is_absolute())
        {
            return tex.string();
        }

        std::filesystem::path base(m_scene_path);
        base = base.parent_path();
        return (base / tex).string();
    }

} // namespace hybrid::assets
