#include <gtest/gtest.h>

#include "assets/AssimpTextureCache.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

namespace
{
    class CountingImageLoader final : public hybrid::assets::IAssetLoader
    {
    public:
        std::type_index Type() const override
        {
            return std::type_index(typeid(hybrid::assets::ImageAsset));
        }

        bool SupportsExtension(std::string_view extension) const override
        {
            return extension == "png" || extension == "jpg" || extension == "jpeg";
        }

        std::shared_ptr<void> Load(const hybrid::assets::AssetLoadRequest &, hybrid::assets::IAssetDataSource *) override
        {
            ++load_calls;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));

            auto image = std::make_shared<hybrid::assets::ImageAsset>();
            image->width = 1;
            image->height = 1;
            image->channels = 4;
            image->pixels = {255, 255, 255, 255};
            return image;
        }

        std::atomic<int> load_calls{0};
    };
} // namespace

TEST(AssimpTextureCacheTest, ReturnsInvalidHandleWithoutAssetManager)
{
    hybrid::assets::AssimpTextureCache cache(nullptr, "C:/project/assets/scene.gltf");
    auto handle = cache.GetOrLoad("textures/albedo.png");
    EXPECT_FALSE(handle.IsValid());
}

TEST(AssimpTextureCacheTest, ReusesHandleForSameRelativeTexturePath)
{
    hybrid::assets::AssetManager assets;
    auto loader = std::make_unique<CountingImageLoader>();
    auto *loader_ptr = loader.get();
    assets.RegisterLoader(std::move(loader));

    hybrid::assets::AssimpTextureCache cache(&assets, "C:/project/assets/scene.gltf");

    auto first = cache.GetOrLoad("textures/albedo.png");
    auto second = cache.GetOrLoad("textures/albedo.png");

    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(second.IsValid());
    EXPECT_EQ(first.Id(), second.Id());
    EXPECT_EQ(loader_ptr->load_calls.load(std::memory_order_relaxed), 1);
}

TEST(AssimpTextureCacheTest, ReusesHandleBetweenRelativeAndAbsolutePaths)
{
    hybrid::assets::AssetManager assets;
    auto loader = std::make_unique<CountingImageLoader>();
    auto *loader_ptr = loader.get();
    assets.RegisterLoader(std::move(loader));

    const std::string scene_path = "C:/project/assets/scene.gltf";
    hybrid::assets::AssimpTextureCache cache(&assets, scene_path);

    const std::string relative_path = "textures/albedo.png";
    const std::string absolute_path =
        (std::filesystem::path(scene_path).parent_path() / std::filesystem::path(relative_path)).string();

    auto from_relative = cache.GetOrLoad(relative_path);
    auto from_absolute = cache.GetOrLoad(absolute_path);

    ASSERT_TRUE(from_relative.IsValid());
    ASSERT_TRUE(from_absolute.IsValid());
    EXPECT_EQ(from_relative.Id(), from_absolute.Id());
    EXPECT_EQ(loader_ptr->load_calls.load(std::memory_order_relaxed), 1);
}

TEST(AssimpTextureCacheTest, ConcurrentRequestsReturnStableCachedHandle)
{
    hybrid::assets::AssetManager assets;
    auto loader = std::make_unique<CountingImageLoader>();
    auto *loader_ptr = loader.get();
    assets.RegisterLoader(std::move(loader));

    hybrid::assets::AssimpTextureCache cache(&assets, "C:/project/assets/scene.gltf");

    constexpr int kThreads = 8;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    std::vector<hybrid::assets::AssetHandle<hybrid::assets::ImageAsset>> handles(kThreads);

    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back([&cache, &handles, i]()
                             { handles[i] = cache.GetOrLoad("textures/albedo.png"); });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    for (const auto &handle : handles)
    {
        ASSERT_TRUE(handle.IsValid());
    }

    const auto canonical = cache.GetOrLoad("textures/albedo.png");
    ASSERT_TRUE(canonical.IsValid());
    for (const auto &handle : handles)
    {
        EXPECT_EQ(handle.Id(), canonical.Id());
    }

    EXPECT_GE(loader_ptr->load_calls.load(std::memory_order_relaxed), 1);
}

