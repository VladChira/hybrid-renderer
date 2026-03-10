#pragma once

#include "assets/AssetManager.h"
#include "assets/ImageAsset.h"

#include <array>

namespace hybrid::assets
{

    class StbImageLoader final : public IAssetLoader
    {
    public:
        std::type_index Type() const override;
        bool SupportsExtension(std::string_view extension) const override;
        std::shared_ptr<void> Load(const AssetLoadRequest &request, IAssetDataSource *data_source) override;

    private:
        static constexpr std::array<std::string_view, 10> kExtensions = {
            "png",
            "jpg",
            "jpeg",
            "bmp",
            "tga",
            "gif",
            "psd",
            "hdr",
            "pic",
            "pnm"};
    };

} // namespace hybrid::assets
