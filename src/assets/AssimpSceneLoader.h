#pragma once

#include "assets/AssetManager.h"
#include "core/scene/SceneTypes.h"

#include <assimp/scene.h>

namespace hybrid::assets
{

    class AssimpSceneLoader final : public IAssetLoader
    {
    public:
        explicit AssimpSceneLoader(AssetManager *asset_manager);

        std::type_index Type() const override;
        bool SupportsExtension(std::string_view extension) const override;
        std::shared_ptr<void> Load(const AssetLoadRequest &request, IAssetDataSource *data_source) override;

    private:
        void LogSceneSummary(const aiScene *scene) const;

        AssetManager *m_assets = nullptr;
    };

} // namespace hybrid::assets
