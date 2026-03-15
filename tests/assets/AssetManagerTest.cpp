#include <gtest/gtest.h>

#include "assets/AssetManager.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    struct DummyAsset
    {
        int value = 0;
    };
} // namespace

TEST(AssetManagerTest, AddGetUnloadRoundTrip)
{
    hybrid::assets::AssetManager assets;

    auto handle = assets.Add<DummyAsset>("dummy.asset", std::make_shared<DummyAsset>(DummyAsset{42}));
    ASSERT_TRUE(handle.IsValid());

    auto *asset = handle.Get();
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->value, 42);
    EXPECT_EQ(assets.GetPath(handle.Id()), "dummy.asset");
    EXPECT_TRUE(assets.IsLoaded(handle.Id()));

    EXPECT_TRUE(assets.Unload(handle.Id()));
    EXPECT_FALSE(assets.IsLoaded(handle.Id()));
    EXPECT_EQ(assets.Get<DummyAsset>(handle.Id()), nullptr);
}

TEST(AssetManagerTest, ConcurrentAddAndGetIsSafe)
{
    hybrid::assets::AssetManager assets;

    constexpr int kThreads = 8;
    constexpr int kPerThread = 64;

    std::atomic<bool> ok{true};
    std::mutex results_mutex;
    std::vector<std::pair<hybrid::assets::AssetId, int>> results;
    results.reserve(kThreads * kPerThread);

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([t, &assets, &ok, &results, &results_mutex]()
                             {
            for (int i = 0; i < kPerThread; ++i)
            {
                const int value = t * kPerThread + i;
                auto handle = assets.Add<DummyAsset>("dummy.concurrent", std::make_shared<DummyAsset>(DummyAsset{value}));
                if (!handle.IsValid())
                {
                    ok.store(false, std::memory_order_relaxed);
                    continue;
                }
                auto *asset = assets.Get<DummyAsset>(handle.Id());
                if (!asset || asset->value != value)
                {
                    ok.store(false, std::memory_order_relaxed);
                }

                std::lock_guard<std::mutex> lock(results_mutex);
                results.emplace_back(handle.Id(), value);
            } });
    }

    for (auto &thread : threads)
    {
        thread.join();
    }

    EXPECT_TRUE(ok.load(std::memory_order_relaxed));
    for (const auto &entry : results)
    {
        auto *asset = assets.Get<DummyAsset>(entry.first);
        ASSERT_NE(asset, nullptr);
        EXPECT_EQ(asset->value, entry.second);
        EXPECT_TRUE(assets.IsLoaded(entry.first));
    }
}
