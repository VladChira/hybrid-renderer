#include "PerformancePanel.h"

#include "core/Profiling.h"
#include "core/PerformanceTelemetry.h"
#include "core/ResourceMonitor.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <vector>

namespace hybrid::ui
{

    PerformancePanel::PerformancePanel()
        : Panel("Performance")
    {
    }

    void PerformancePanel::DrawContents(PanelContext &context)
    {
        HYBRID_PROFILE_ZONE_N("PerformancePanel::DrawContents");
        (void)context;

        std::vector<core::FramePerformanceSample> frame_samples;
        {
            HYBRID_PROFILE_ZONE_N("PerformancePanel::GetFrameSamples");
            frame_samples = core::PerformanceTelemetry::GetFrameSamples();
        }
        if (frame_samples.empty())
        {
            ImGui::TextUnformatted("Waiting for frame samples...");
        }
        else
        {
            std::vector<double> frame_times;
            std::vector<double> cpu_frame_ms;
            std::vector<double> fps_values;
            double cpu_frame_ms_sum = 0.0;
            double fps_sum = 0.0;
            double gpu_frame_ms_sum = 0.0;
            double shadow_gpu_ms_sum = 0.0;
            uint32_t gpu_frame_sample_count = 0;
            uint32_t shadow_gpu_sample_count = 0;
            {
                HYBRID_PROFILE_ZONE_N("PerformancePanel::BuildFrameSeries");
                frame_times.reserve(frame_samples.size());
                cpu_frame_ms.reserve(frame_samples.size());
                fps_values.reserve(frame_samples.size());
                for (const auto &sample : frame_samples)
                {
                    frame_times.push_back(sample.time_seconds);
                    cpu_frame_ms.push_back(sample.renderer_cpu_frame_ms);
                    fps_values.push_back(sample.fps);
                    cpu_frame_ms_sum += sample.renderer_cpu_frame_ms;
                    fps_sum += sample.fps;

                    if (sample.renderer_gpu_frame_ms_valid)
                    {
                        gpu_frame_ms_sum += sample.renderer_gpu_frame_ms;
                        gpu_frame_sample_count++;
                    }
                    if (sample.raytrace_shadow_gpu_ms_valid)
                    {
                        shadow_gpu_ms_sum += sample.raytrace_shadow_gpu_ms;
                        shadow_gpu_sample_count++;
                    }
                }
            }

            const double latest_cpu_frame_ms = cpu_frame_ms.back();
            const double latest_fps = fps_values.back();
            const double avg_cpu_frame_ms = cpu_frame_ms_sum / static_cast<double>(frame_samples.size());
            const double avg_fps = fps_sum / static_cast<double>(frame_samples.size());
            const double avg_gpu_frame_ms = gpu_frame_sample_count > 0
                                                ? gpu_frame_ms_sum / static_cast<double>(gpu_frame_sample_count)
                                                : 0.0;
            const double avg_shadow_gpu_ms = shadow_gpu_sample_count > 0
                                                 ? shadow_gpu_ms_sum / static_cast<double>(shadow_gpu_sample_count)
                                                 : 0.0;
            const core::FramePerformanceSample &latest_sample = frame_samples.back();

            ImGui::Text("Renderer CPU submit: %.2f ms (avg %.2f ms) | %.1f FPS (avg %.1f FPS)",
                        latest_cpu_frame_ms,
                        avg_cpu_frame_ms,
                        latest_fps,
                        avg_fps);
            if (latest_sample.renderer_gpu_frame_ms_valid)
            {
                ImGui::Text("Renderer GPU frame: %.2f ms (avg %.2f ms over %u samples)",
                            latest_sample.renderer_gpu_frame_ms,
                            avg_gpu_frame_ms,
                            gpu_frame_sample_count);
            }
            else
            {
                ImGui::TextUnformatted("Renderer GPU frame: waiting for timer results...");
            }
            if (latest_sample.raytrace_shadow_gpu_ms_valid)
            {
                ImGui::Text("Raytraced shadow GPU: %.2f ms (avg %.2f ms over %u samples)",
                            latest_sample.raytrace_shadow_gpu_ms,
                            avg_shadow_gpu_ms,
                            shadow_gpu_sample_count);
            }
            else
            {
                ImGui::TextUnformatted("Raytraced shadow GPU: waiting for timer results...");
            }
            ImGui::Text("Draws: %u | Prims: %u | Tris: %llu | Verts: %llu",
                        latest_sample.draw_calls,
                        latest_sample.submitted_primitives,
                        static_cast<unsigned long long>(latest_sample.submitted_triangles),
                        static_cast<unsigned long long>(latest_sample.submitted_vertices));
            ImGui::Text("GBuffer uniforms: %u | texture binds: %u",
                        latest_sample.gbuffer_uniform_updates,
                        latest_sample.gbuffer_texture_binds);
            ImGui::Text("GBuffer cache misses: prim %u / tex %u | uploads: prim %u / tex %u",
                        latest_sample.gbuffer_primitive_cache_misses,
                        latest_sample.gbuffer_texture_cache_misses,
                        latest_sample.gbuffer_primitive_uploads,
                        latest_sample.gbuffer_texture_uploads);

            const double frame_x_min = frame_times.front();
            const double frame_x_max = frame_times.back();

            double max_cpu_frame_ms = 0.0;
            double max_fps = 0.0;
            for (size_t i = 0; i < cpu_frame_ms.size(); ++i)
            {
                max_cpu_frame_ms = std::max(max_cpu_frame_ms, cpu_frame_ms[i]);
                max_fps = std::max(max_fps, fps_values[i]);
            }

            const double frame_ms_y_max = max_cpu_frame_ms > 1e-3 ? max_cpu_frame_ms * 1.1 : 1.0;
            const double fps_y_max = max_fps > 1e-3 ? max_fps * 1.1 : 1.0;

            if (ImPlot::BeginPlot("Renderer CPU Submit Time (ms)", ImVec2(-1, 160), ImPlotFlags_NoLegend))
            {
                HYBRID_PROFILE_ZONE_N("PerformancePanel::PlotFrameMs");
                ImPlot::SetupAxes(nullptr, "ms",
                                  ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickLabels,
                                  ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxesLimits(frame_x_min, frame_x_max, 0.0, frame_ms_y_max, ImPlotCond_Always);
                ImPlot::PlotLine("CPU Submit", frame_times.data(), cpu_frame_ms.data(), static_cast<int>(frame_samples.size()));
                ImPlot::EndPlot();
            }

            if (ImPlot::BeginPlot("Renderer FPS", ImVec2(-1, 160), ImPlotFlags_NoLegend))
            {
                HYBRID_PROFILE_ZONE_N("PerformancePanel::PlotFps");
                ImPlot::SetupAxes(nullptr, "FPS",
                                  ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickLabels,
                                  ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxesLimits(frame_x_min, frame_x_max, 0.0, fps_y_max, ImPlotCond_Always);
                ImPlot::PlotLine("FPS", frame_times.data(), fps_values.data(), static_cast<int>(frame_samples.size()));
                ImPlot::EndPlot();
            }
        }

        std::vector<core::ResourceSample> ram_samples;
        {
            HYBRID_PROFILE_ZONE_N("PerformancePanel::GetRamSamples");
            ram_samples = core::ResourceMonitor::GetSamples();
        }
        if (ram_samples.empty())
        {
            ImGui::TextUnformatted("Waiting for RAM samples...");
            return;
        }

        std::vector<double> ram_times;
        std::vector<double> ram_mb;
        double max_ram = 0.0;
        {
            HYBRID_PROFILE_ZONE_N("PerformancePanel::BuildRamSeries");
            ram_times.reserve(ram_samples.size());
            ram_mb.reserve(ram_samples.size());
            for (const auto &sample : ram_samples)
            {
                ram_times.push_back(sample.time_seconds);
                ram_mb.push_back(sample.ram_mb);
                max_ram = std::max(max_ram, sample.ram_mb);
            }
        }

        const double x_min = ram_times.front();
        const double x_max = ram_times.back();
        const double y_max = max_ram > 1e-3 ? max_ram * 1.1 : 1.0;

        if (ImPlot::BeginPlot("RAM Usage (MB)", ImVec2(-1, 160), ImPlotFlags_NoLegend))
        {
            HYBRID_PROFILE_ZONE_N("PerformancePanel::PlotRam");
            ImPlot::SetupAxes(nullptr, "MB",
                              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickLabels,
                              ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxesLimits(x_min, x_max, 0.0, y_max, ImPlotCond_Always);
            ImPlot::PlotLine("RAM", ram_times.data(), ram_mb.data(), static_cast<int>(ram_samples.size()));
            ImPlot::EndPlot();
        }
    }

} // namespace hybrid::ui
