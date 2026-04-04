#include "core/PerformanceTelemetry.h"

#include <mutex>

namespace hybrid::core
{
    namespace
    {
        constexpr size_t kDefaultSampleCapacity = 600;
        std::mutex s_frame_samples_mutex;
    }

    bool PerformanceTelemetry::s_initialized = false;
    size_t PerformanceTelemetry::s_capacity = kDefaultSampleCapacity;
    std::vector<FramePerformanceSample> PerformanceTelemetry::s_frame_samples;

    void PerformanceTelemetry::Init()
    {
        if (s_initialized)
        {
            return;
        }

        std::lock_guard lock(s_frame_samples_mutex);
        s_frame_samples.clear();
        s_frame_samples.reserve(s_capacity);
        s_initialized = true;
    }

    void PerformanceTelemetry::Shutdown()
    {
        if (!s_initialized)
        {
            return;
        }

        s_initialized = false;
    }

    void PerformanceTelemetry::Clear()
    {
        std::lock_guard lock(s_frame_samples_mutex);
        s_frame_samples.clear();
    }

    void PerformanceTelemetry::RecordFrameSample(const FramePerformanceSample &sample)
    {
        if (!s_initialized)
        {
            return;
        }

        std::lock_guard lock(s_frame_samples_mutex);
        if (s_frame_samples.size() >= s_capacity && !s_frame_samples.empty())
        {
            s_frame_samples.erase(s_frame_samples.begin());
        }
        s_frame_samples.push_back(sample);
    }

    std::vector<FramePerformanceSample> PerformanceTelemetry::GetFrameSamples()
    {
        std::lock_guard lock(s_frame_samples_mutex);
        return s_frame_samples;
    }

    size_t PerformanceTelemetry::GetCapacity()
    {
        return s_capacity;
    }

} // namespace hybrid::core
