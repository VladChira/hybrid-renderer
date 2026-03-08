#include "core/ResourceMonitor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace hybrid::core
{
    namespace
    {
        constexpr size_t kDefaultSampleCapacity = 600;
        constexpr double kSampleIntervalSeconds = 0.1;
    }

    bool ResourceMonitor::s_initialized = false;
    size_t ResourceMonitor::s_capacity = kDefaultSampleCapacity;
    std::vector<ResourceSample> ResourceMonitor::s_samples;
    std::mutex s_samples_mutex;
    std::atomic_bool s_running{false};
    std::thread s_worker;
    std::chrono::steady_clock::time_point s_start_time;

    void ResourceMonitor::Init()
    {
        if (s_initialized)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(s_samples_mutex);
            s_samples.clear();
            s_samples.reserve(s_capacity);
        }
        s_start_time = std::chrono::steady_clock::now();
        s_running.store(true);
        s_worker = std::thread([]() {
            auto next_tick = std::chrono::steady_clock::now();
            while (s_running.load())
            {
                next_tick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(kSampleIntervalSeconds));

                ResourceSample sample{};
                const auto now = std::chrono::steady_clock::now();
                sample.time_seconds =
                    std::chrono::duration<double>(now - s_start_time).count();
                sample.ram_mb = QueryProcessRamMB();

                {
                    std::lock_guard<std::mutex> lock(s_samples_mutex);
                    if (s_samples.size() >= s_capacity && !s_samples.empty())
                    {
                        s_samples.erase(s_samples.begin());
                    }
                    s_samples.push_back(sample);
                }

                std::this_thread::sleep_until(next_tick);
            }
        });

        s_initialized = true;
    }

    void ResourceMonitor::Shutdown()
    {
        if (!s_initialized)
        {
            return;
        }

        s_running.store(false);
        if (s_worker.joinable())
        {
            s_worker.join();
        }
        s_initialized = false;
    }

    void ResourceMonitor::Clear()
    {
        std::lock_guard<std::mutex> lock(s_samples_mutex);
        s_samples.clear();
    }

    std::vector<ResourceSample> ResourceMonitor::GetSamples()
    {
        std::lock_guard<std::mutex> lock(s_samples_mutex);
        return s_samples;
    }

    size_t ResourceMonitor::GetCapacity()
    {
        return s_capacity;
    }

    double ResourceMonitor::GetCurrentRamMB()
    {
        return QueryProcessRamMB();
    }

    double ResourceMonitor::QueryProcessRamMB()
    {
#ifdef _WIN32
        if (PROCESS_MEMORY_COUNTERS counters{}; GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0)
        {
            return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
        }
        return 0.0;
#else
        return 0.0;
#endif
    }

} // namespace hybrid::core
