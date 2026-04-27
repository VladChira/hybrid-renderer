#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hybrid::core
{

    struct FramePerformanceSample
    {
        double time_seconds = 0.0;
        double renderer_cpu_frame_ms = 0.0;
        double renderer_gpu_frame_ms = 0.0;
        bool renderer_gpu_frame_ms_valid = false;
        double raytrace_shadow_gpu_ms = 0.0;
        bool raytrace_shadow_gpu_ms_valid = false;
        double fps = 0.0;
        uint32_t draw_calls = 0;
        uint32_t submitted_primitives = 0;
        uint64_t submitted_vertices = 0;
        uint64_t submitted_triangles = 0;
        uint32_t gbuffer_uniform_updates = 0;
        uint32_t gbuffer_texture_binds = 0;
        uint32_t gbuffer_primitive_cache_misses = 0;
        uint32_t gbuffer_texture_cache_misses = 0;
        uint32_t gbuffer_primitive_uploads = 0;
        uint32_t gbuffer_texture_uploads = 0;
    };

    class PerformanceTelemetry
    {
    public:
        static void Init();
        static void Shutdown();
        static void Clear();

        static void RecordFrameSample(const FramePerformanceSample &sample);
        static std::vector<FramePerformanceSample> GetFrameSamples();
        static size_t GetCapacity();

    private:
        static bool s_initialized;
        static size_t s_capacity;
        static std::vector<FramePerformanceSample> s_frame_samples;
    };

} // namespace hybrid::core
