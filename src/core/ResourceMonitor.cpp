#include "core/ResourceMonitor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/task.h>
#include <mach/task_info.h>
#elif defined(__linux__)
#include <unistd.h>
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
            std::lock_guard lock(s_samples_mutex);
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
                    std::lock_guard lock(s_samples_mutex);
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
        std::lock_guard lock(s_samples_mutex);
        s_samples.clear();
    }

    std::vector<ResourceSample> ResourceMonitor::GetSamples()
    {
        std::lock_guard lock(s_samples_mutex);
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
#elif defined(__APPLE__)
        // macOS has no procfs; use Mach's task_info. resident_size is the
        // resident set size in bytes — the closest equivalent to Windows
        // WorkingSetSize and Linux RSS.
        mach_task_basic_info_data_t info{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                      reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        {
            return 0.0;
        }
        return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
#elif defined(__linux__)
        // /proc/self/statm: size resident shared text lib data dt (all in pages)
        // We use resident pages (RSS) as the closest equivalent to Windows WorkingSetSize.
        std::ifstream statm("/proc/self/statm");
        if (!statm.is_open())
        {
            return 0.0;
        }

        std::size_t total_pages = 0;
        std::size_t resident_pages = 0;
        statm >> total_pages >> resident_pages;
        if (!statm.good() && !statm.eof())
        {
            return 0.0;
        }

        const long page_size = ::sysconf(_SC_PAGESIZE);
        if (page_size <= 0)
        {
            return 0.0;
        }

        const double bytes = static_cast<double>(resident_pages) * static_cast<double>(page_size);
        return bytes / (1024.0 * 1024.0);
#else
        return 0.0;
#endif
    }

} // namespace hybrid::core
