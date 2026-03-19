#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <mutex>
#include <vector>

namespace hybrid::assets
{

    struct AssetId
    {
        uint64_t value = 0;

        bool IsValid() const { return value != 0; }

        friend bool operator==(AssetId lhs, AssetId rhs)
        {
            return lhs.value == rhs.value;
        }

        friend bool operator!=(AssetId lhs, AssetId rhs)
        {
            return lhs.value != rhs.value;
        }
    };

    struct AssetIdHash
    {
        size_t operator()(AssetId id) const noexcept
        {
            return std::hash<uint64_t>{}(id.value);
        }
    };

    class IAssetDataSource
    {
    public:
        virtual ~IAssetDataSource() = default;

        virtual bool ReadAllBytes(const std::string &path, std::vector<std::byte> &out_bytes) = 0;
    };

    struct AssetLoadRequest
    {
        std::string path;
        std::string extension;
    };

    class IAssetLoader
    {
    public:
        virtual ~IAssetLoader() = default;

        virtual std::type_index Type() const = 0;
        virtual bool SupportsExtension(std::string_view extension) const = 0;
        virtual std::shared_ptr<void> Load(const AssetLoadRequest &request, IAssetDataSource *data_source) = 0;
    };

    template <typename T>
    class AssetHandle;

    class AssetManager
    {
    public:
        AssetManager();

        void SetDataSource(std::shared_ptr<IAssetDataSource> data_source);
        void RegisterLoader(std::unique_ptr<IAssetLoader> loader);

        AssetId Load(const std::string &path, std::type_index type);

        template <typename T>
        AssetId Load(const std::string &path)
        {
            return Load(path, std::type_index(typeid(T)));
        }

        template <typename T>
        AssetHandle<T> LoadHandle(const std::string &path);

        template <typename T>
        AssetHandle<T> Add(std::string_view path, std::shared_ptr<T> asset);

        template <typename T>
        T *Get(AssetId id);

        template <typename T>
        const T *Get(AssetId id) const;

        template <typename T>
        AssetHandle<T> GetHandle(AssetId id);

        bool Unload(AssetId id);
        bool IsLoaded(AssetId id) const;
        std::string GetPath(AssetId id) const;

    private:
        struct AssetRecord
        {
            AssetId id;
            std::string path;
            std::type_index type{typeid(void)};
            std::shared_ptr<void> asset;
        };

        IAssetLoader *FindLoader(std::type_index type, std::string_view extension) const;
        AssetRecord *FindRecord(AssetId id);
        const AssetRecord *FindRecord(AssetId id) const;

        mutable std::mutex m_mutex;
        std::unordered_map<AssetId, AssetRecord, AssetIdHash> m_assets;
        std::vector<std::unique_ptr<IAssetLoader>> m_loaders;
        std::shared_ptr<IAssetDataSource> m_data_source;
        uint64_t m_next_id = 1;
    };

    template <typename T>
    class AssetHandle
    {
    public:
        AssetHandle() = default;
        AssetHandle(AssetId id, AssetManager *manager) : m_id(id), m_manager(manager) {}

        AssetId Id() const { return m_id; }

        bool IsValid() const
        {
            return m_manager != nullptr && m_id.IsValid();
        }

        T *Get()
        {
            return m_manager ? m_manager->Get<T>(m_id) : nullptr;
        }

        const T *Get() const
        {
            return m_manager ? m_manager->Get<T>(m_id) : nullptr;
        }

        explicit operator bool() const
        {
            return Get() != nullptr;
        }

    private:
        AssetId m_id{};
        AssetManager *m_manager = nullptr;
    };

    template <typename T>
    AssetHandle<T> AssetManager::LoadHandle(const std::string &path)
    {
        return AssetHandle<T>(Load<T>(path), this);
    }

    template <typename T>
    AssetHandle<T> AssetManager::Add(std::string_view path, std::shared_ptr<T> asset)
    {
        if (!asset)
        {
            return {};
        }

        std::lock_guard lock(m_mutex);
        AssetId id{m_next_id++};
        AssetRecord record{};
        record.id = id;
        record.path = path;
        record.type = std::type_index(typeid(T));
        record.asset = std::move(asset);

        m_assets.try_emplace(id, std::move(record));
        return AssetHandle<T>(id, this);
    }

    template <typename T>
    T *AssetManager::Get(AssetId id)
    {
        std::lock_guard lock(m_mutex);
        const auto *record = FindRecord(id);
        if (!record || record->type != std::type_index(typeid(T)))
        {
            return nullptr;
        }
        return static_cast<T *>(record->asset.get());
    }

    template <typename T>
    const T *AssetManager::Get(AssetId id) const
    {
        std::lock_guard lock(m_mutex);
        const auto *record = FindRecord(id);
        if (!record || record->type != std::type_index(typeid(T)))
        {
            return nullptr;
        }
        return static_cast<const T *>(record->asset.get());
    }

    template <typename T>
    AssetHandle<T> AssetManager::GetHandle(AssetId id)
    {
        return AssetHandle<T>(id, this);
    }

} // namespace hybrid::assets
