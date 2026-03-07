#include "assets/AssetManager.h"
#include "utils/PathUtils.h"

#include <utility>

namespace hybrid::assets
{

    AssetManager::AssetManager() = default;

    void AssetManager::SetDataSource(std::shared_ptr<IAssetDataSource> data_source)
    {
        m_data_source = std::move(data_source);
    }

    void AssetManager::RegisterLoader(std::unique_ptr<IAssetLoader> loader)
    {
        if (!loader)
        {
            return;
        }
        m_loaders.push_back(std::move(loader));
    }

    AssetId AssetManager::Load(const std::string &path, std::type_index type)
    {
        const std::string extension = utils::ExtractExtension(path);
        IAssetLoader *loader = FindLoader(type, extension);
        if (!loader)
        {
            return {};
        }

        AssetLoadRequest request{};
        request.path = path;
        request.extension = extension;

        std::shared_ptr<void> asset = loader->Load(request, m_data_source.get());
        if (!asset)
        {
            return {};
        }

        AssetId id{m_next_id++};
        AssetRecord record{};
        record.id = id;
        record.path = path;
        record.type = type;
        record.asset = std::move(asset);

        m_assets.try_emplace(id, std::move(record));
        return id;
    }

    bool AssetManager::Unload(AssetId id)
    {
        return m_assets.erase(id) > 0;
    }

    bool AssetManager::IsLoaded(AssetId id) const
    {
        return m_assets.find(id) != m_assets.end();
    }

    std::string AssetManager::GetPath(AssetId id) const
    {
        const auto *record = FindRecord(id);
        if (!record)
        {
            return {};
        }
        return record->path;
    }

    AssetManager::AssetRecord *AssetManager::FindRecord(AssetId id)
    {
        auto it = m_assets.find(id);
        if (it == m_assets.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    const AssetManager::AssetRecord *AssetManager::FindRecord(AssetId id) const
    {
        auto it = m_assets.find(id);
        if (it == m_assets.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    IAssetLoader *AssetManager::FindLoader(std::type_index type, std::string_view extension) const
    {
        for (const auto &loader : m_loaders)
        {
            if (!loader)
            {
                continue;
            }
            if (loader->Type() == type && loader->SupportsExtension(extension))
            {
                return loader.get();
            }
        }
        return nullptr;
    }

} // namespace hybrid::assets
