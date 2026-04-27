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
            std::vector<double> frame_ms;
            std::vector<double> fps_values;
            double frame_ms_sum = 0.0;
            double fps_sum = 0.0;
            {
                HYBRID_PROFILE_ZONE_N("PerformancePanel::BuildFrameSeries");
                frame_times.reserve(frame_samples.size());
                frame_ms.reserve(frame_samples.size());
                fps_values.reserve(frame_samples.size());
                for (const auto &sample : frame_samples)
                {
                    frame_times.push_back(sample.time_seconds);
                    frame_ms.push_back(sample.renderer_cpu_frame_ms);
                    fps_values.push_back(sample.fps);
                    frame_ms_sum += sample.renderer_cpu_frame_ms;
                    fps_sum += sample.fps;
                }
            }

            const double latest_frame_ms = frame_ms.back();
            const double latest_fps = fps_values.back();
            const double avg_frame_ms = frame_ms_sum / static_cast<double>(frame_samples.size());
            const double avg_fps = fps_sum / static_cast<double>(frame_samples.size());
            const core::FramePerformanceSample &latest_sample = frame_samples.back();

            ImGui::Text("Renderer: %.2f ms (avg %.2f ms) | %.1f FPS (avg %.1f FPS)",
                        latest_frame_ms,
                        avg_frame_ms,
                        latest_fps,
                        avg_fps);
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

            double max_frame_ms = 0.0;
            double max_fps = 0.0;
            for (size_t i = 0; i < frame_ms.size(); ++i)
            {
                max_frame_ms = std::max(max_frame_ms, frame_ms[i]);
                max_fps = std::max(max_fps, fps_values[i]);
            }

            const double frame_ms_y_max = max_frame_ms > 1e-3 ? max_frame_ms * 1.1 : 1.0;
            const double fps_y_max = max_fps > 1e-3 ? max_fps * 1.1 : 1.0;

            if (ImPlot::BeginPlot("Renderer Frame Time (ms)", ImVec2(-1, 160), ImPlotFlags_NoLegend))
            {
                HYBRID_PROFILE_ZONE_N("PerformancePanel::PlotFrameMs");
                ImPlot::SetupAxes(nullptr, "ms",
                                  ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickLabels,
                                  ImPlotAxisFlags_AutoFit);
                ImPlot::SetupAxesLimits(frame_x_min, frame_x_max, 0.0, frame_ms_y_max, ImPlotCond_Always);
                ImPlot::PlotLine("Frame Time", frame_times.data(), frame_ms.data(), static_cast<int>(frame_samples.size()));
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
