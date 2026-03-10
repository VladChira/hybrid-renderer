#include <gtest/gtest.h>

#include "assets/ImageAsset.h"
#include "assets/StbImageLoader.h"

#include <fstream>

namespace
{
    class FileDataSource final : public hybrid::assets::IAssetDataSource
    {
    public:
        bool ReadAllBytes(const std::string &path, std::vector<std::byte> &out_bytes) override
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
    };
} // namespace

TEST(StbImageLoaderTest, LoadsWhiteImageFirstPixelIsWhite)
{
    const std::string image_path = std::string(HYBRID_TEST_RESOURCES_DIR) + "/white.png";

    hybrid::assets::AssetLoadRequest request{};
    request.path = image_path;
    request.extension = "png";

    FileDataSource data_source;
    hybrid::assets::StbImageLoader loader;

    auto asset_any = loader.Load(request, &data_source);
    ASSERT_NE(asset_any, nullptr);

    auto *image = static_cast<hybrid::assets::ImageAsset *>(asset_any.get());
    ASSERT_NE(image, nullptr);
    EXPECT_FALSE(image->is_hdr);
    ASSERT_EQ(image->channels, 4);
    ASSERT_EQ(image->width, 640);
    ASSERT_EQ(image->height, 480);
    ASSERT_EQ(image->pixels.size(), 1228800);

    const uint8_t r = image->pixels[0];
    const uint8_t g = image->channels > 1 ? image->pixels[1] : image->pixels[0];
    const uint8_t b = image->channels > 2 ? image->pixels[2] : image->pixels[0];
    const uint8_t a = image->channels > 3 ? image->pixels[3] : 255;

    EXPECT_EQ(r, 255);
    EXPECT_EQ(g, 255);
    EXPECT_EQ(b, 255);
    EXPECT_EQ(a, 255);
}

TEST(StbImageLoaderTest, LoadsHdriCorrectly)
{
    const std::string image_path = std::string(HYBRID_TEST_RESOURCES_DIR) + "/ticknock_01_1k.hdr";

    hybrid::assets::AssetLoadRequest request{};
    request.path = image_path;
    request.extension = "hdr";

    FileDataSource data_source;
    hybrid::assets::StbImageLoader loader;

    auto asset_any = loader.Load(request, &data_source);
    ASSERT_NE(asset_any, nullptr);

    auto *image = static_cast<hybrid::assets::ImageAsset *>(asset_any.get());
    ASSERT_NE(image, nullptr);
    EXPECT_TRUE(image->is_hdr);
}

TEST(StbImageLoaderTest, LoadsTransparentImagesCorrectly)
{
    const std::string image_path = std::string(HYBRID_TEST_RESOURCES_DIR) + "/hat.png";

    hybrid::assets::AssetLoadRequest request{};
    request.path = image_path;
    request.extension = "png";

    FileDataSource data_source;
    hybrid::assets::StbImageLoader loader;

    auto asset_any = loader.Load(request, &data_source);
    ASSERT_NE(asset_any, nullptr);

    auto *image = static_cast<hybrid::assets::ImageAsset *>(asset_any.get());
    ASSERT_NE(image, nullptr);
    

    ASSERT_EQ(image->channels, 4);
    ASSERT_EQ(image->pixels[3], 0); // first pixel is transparent
}
