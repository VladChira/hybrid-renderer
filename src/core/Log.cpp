#include "core/Log.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <deque>
#include <vector>

namespace hybrid::core
{
    namespace
    {
        constexpr size_t kInMemoryLogCapacity = 1024;

        class InMemoryCircularSink final : public spdlog::sinks::base_sink<std::mutex>
        {
        public:
            explicit InMemoryCircularSink(size_t capacity)
                : capacity_(capacity)
            {
            }

            std::vector<std::string> Snapshot()
            {
                std::lock_guard lock(spdlog::sinks::base_sink<std::mutex>::mutex_);
                return std::vector<std::string>(buffer_.begin(), buffer_.end());
            }

            size_t capacity() const
            {
                return capacity_;
            }

        protected:
            void sink_it_(const spdlog::details::log_msg &msg) override
            {
                if (capacity_ == 0)
                    return;

                spdlog::memory_buf_t formatted;
                base_sink<std::mutex>::formatter_->format(msg, formatted);

                if (buffer_.size() >= capacity_)
                    buffer_.pop_front();

                buffer_.emplace_back(formatted.data(), formatted.size());
            }

            void flush_() override {}

        private:
            std::deque<std::string> buffer_;
            size_t capacity_;
        };
    } // namespace

    std::shared_ptr<spdlog::logger> Log::s_logger;
    std::shared_ptr<spdlog::sinks::sink> Log::s_memory_sink;

    void Log::Init()
    {
        if (s_logger)
            return;

        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("hybrid.log", true);
        auto memory_sink = std::make_shared<InMemoryCircularSink>(kInMemoryLogCapacity);

        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(console_sink);
        sinks.push_back(file_sink);
        sinks.push_back(memory_sink);

        s_logger = std::make_shared<spdlog::logger>("hybrid", sinks.begin(), sinks.end());
        spdlog::register_logger(s_logger);
        s_memory_sink = memory_sink;

        s_logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] %v%$");
        s_logger->set_level(spdlog::level::trace);
        s_logger->flush_on(spdlog::level::info);
    }

    std::vector<std::string> Log::GetInMemoryLog()
    {
        if (!s_memory_sink)
            return {};

        auto memory_sink = std::static_pointer_cast<InMemoryCircularSink>(s_memory_sink);
        return memory_sink->Snapshot();
    }

    size_t Log::GetInMemoryCapacity()
    {
        if (!s_memory_sink)
            return 0;

        auto memory_sink = std::static_pointer_cast<InMemoryCircularSink>(s_memory_sink);
        return memory_sink->capacity();
    }

} // namespace hybrid::core
