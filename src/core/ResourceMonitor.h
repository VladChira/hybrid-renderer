#pragma once

#include <vector>

namespace hybrid::core
{

    struct ResourceSample
    {
        double time_seconds = 0.0;
        double ram_mb = 0.0;
    };

    class ResourceMonitor
    {
    public:
        static void Init();
        static void Shutdown();
        static void Clear();

        static std::vector<ResourceSample> GetSamples();
        static size_t GetCapacity();
        static double GetCurrentRamMB();

    private:
        static double QueryProcessRamMB();

        static bool s_initialized;
        static size_t s_capacity;
        static std::vector<ResourceSample> s_samples;
    };

} // namespace hybrid::core
