#pragma once

#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

namespace hybrid::core
{
    class Log
    {
    public:
        static void Init();

        static spdlog::logger &GetLogger() { return *s_logger; }

    private:
        static std::shared_ptr<spdlog::logger> s_logger;
    };

#define LOG_INFO(...) hybrid::core::Log::GetLogger().info(__VA_ARGS__)
#define LOG_WARN(...) hybrid::core::Log::GetLogger().warn(__VA_ARGS__)
#define LOG_ERROR(...) hybrid::core::Log::GetLogger().error(__VA_ARGS__)
#define LOG_CRITICAL(...) hybrid::core::Log::GetLogger().critical(__VA_ARGS__)


} // namespace hybrid::core
