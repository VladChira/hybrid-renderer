#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "assets/AssetManager.h"

#include <cstring>

using ::testing::_;
using ::testing::Return;

namespace
{

    struct MockAsset
    {
        std::string data;
    };

    class MockDataSource : public hybrid::assets::IAssetDataSource
    {
    public:
        MOCK_METHOD(bool, ReadAllBytes, (const std::string &path, std::vector<std::byte> &out_bytes), (override));
    };

    class MockAssetLoader : public hybrid::assets::IAssetLoader
    {
    public:
        std::type_index Type() const override { return typeid(MockAsset); }

        bool SupportsExtension(std::string_view ext) const override
        {
            return ext == "mock";
        }

        std::shared_ptr<void> Load(const hybrid::assets::AssetLoadRequest &req,
                                   hybrid::assets::IAssetDataSource *ds) override
        {
            if (!ds)
                return {};
            std::vector<std::byte> bytes;
            if (!ds->ReadAllBytes(req.path, bytes))
                return {};

            std::string s(reinterpret_cast<const char *>(bytes.data()), bytes.size());

            auto mock_result = std::make_shared<MockAsset>();
            mock_result->data = s;

            return mock_result;
        }
    };

} // namespace

TEST(AssetsMockTest, LoadsThroughDataSource)
{
    auto data_source = std::make_shared<MockDataSource>();

    EXPECT_CALL(*data_source, ReadAllBytes("example.mock", _))
        .WillOnce([](const std::string &, std::vector<std::byte> &out_bytes)
                  {
            const char* payload = "Hello, world!";
            const size_t len = std::strlen(payload);
            out_bytes.resize(len);
            std::memcpy(out_bytes.data(), payload, len);
            return true; });

    hybrid::assets::AssetManager assets;
    assets.SetDataSource(data_source);
    assets.RegisterLoader(std::make_unique<MockAssetLoader>());

    hybrid::assets::AssetId id = assets.Load<MockAsset>("example.mock");

    auto *asset = assets.Get<MockAsset>(id);

    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->data, "Hello, world!");
}

TEST(AssetsMockTest, ReturnsInvalidIdForUnsupportedExtension)
{
    auto data_source = std::make_shared<MockDataSource>();

    EXPECT_CALL(*data_source, ReadAllBytes(_, _)).Times(0);

    hybrid::assets::AssetManager assets;
    assets.SetDataSource(data_source);
    assets.RegisterLoader(std::make_unique<MockAssetLoader>());

    hybrid::assets::AssetId id = assets.Load<MockAsset>("no_extension");

    EXPECT_FALSE(id.IsValid());
    EXPECT_EQ(assets.Get<MockAsset>(id), nullptr);
}

TEST(AssetsMockTest, ReturnsInvalidIdWhenReadFails)
{
    auto data_source = std::make_shared<MockDataSource>();

    EXPECT_CALL(*data_source, ReadAllBytes("bad.mock", _))
        .WillOnce(Return(false));

    hybrid::assets::AssetManager assets;
    assets.SetDataSource(data_source);
    assets.RegisterLoader(std::make_unique<MockAssetLoader>());

    hybrid::assets::AssetId id = assets.Load<MockAsset>("bad.mock");

    EXPECT_FALSE(id.IsValid());
    EXPECT_EQ(assets.Get<MockAsset>(id), nullptr);
}
