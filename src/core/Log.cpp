#include "core/Log.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <vector>

namespace hybrid::core
{
    std::shared_ptr<spdlog::logger> Log::s_logger;

    void Log::Init()
    {
        if (s_logger)
            return;

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("hybrid.log", true);

        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(console_sink);
        sinks.push_back(file_sink);

        s_logger = std::make_shared<spdlog::logger>("hybrid", sinks.begin(), sinks.end());
        spdlog::register_logger(s_logger);

        s_logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] %v%$");
        s_logger->set_level(spdlog::level::trace);
        s_logger->flush_on(spdlog::level::info);
    }

} // namespace hybrid::core
