#include "PerformancePanel.h"

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
        (void)context;

        const auto samples = core::ResourceMonitor::GetSamples();
        if (samples.empty())
        {
            ImGui::TextUnformatted("Waiting for samples...");
            return;
        }

        std::vector<double> times;
        std::vector<double> ram_mb;
        times.reserve(samples.size());
        ram_mb.reserve(samples.size());

        double max_ram = 0.0;
        for (const auto &sample : samples)
        {
            times.push_back(sample.time_seconds);
            ram_mb.push_back(sample.ram_mb);
            max_ram = std::max(max_ram, sample.ram_mb);
        }

        const double x_min = times.front();
        const double x_max = times.back();
        const double y_max = max_ram > 1e-3 ? max_ram * 1.1 : 1.0;

        if (ImPlot::BeginPlot("RAM Usage (MB)", ImVec2(-1, 0), ImPlotFlags_NoLegend))
        {
            ImPlot::SetupAxes(nullptr, "MB",
                              ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_NoTickLabels,
                              ImPlotAxisFlags_AutoFit);
            ImPlot::SetupAxesLimits(x_min, x_max, 0.0, y_max, ImPlotCond_Always);
            ImPlot::PlotLine("RAM", times.data(), ram_mb.data(), static_cast<int>(samples.size()));
            ImPlot::EndPlot();
        }
    }

} // namespace hybrid::ui
