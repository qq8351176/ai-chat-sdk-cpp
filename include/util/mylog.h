#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>

namespace util {

class Logger {
public:
    static void initLogger(
        const std::string& loggerName,
        const std::string& loggerFile,
        spdlog::level::level_enum logLevel = spdlog::level::info);
    static std::shared_ptr<spdlog::logger> getLogger();

private:
    static std::shared_ptr<spdlog::logger> _logger;
    static std::mutex _mutex;
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
};

} // namespace util

#define TRACE(...) \
    ::util::Logger::getLogger()->log( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
        spdlog::level::trace, \
        __VA_ARGS__)

#define DBG(...) \
    ::util::Logger::getLogger()->log( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
        spdlog::level::debug, \
        __VA_ARGS__)

#define INFO(...) \
    ::util::Logger::getLogger()->log( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::info, __VA_ARGS__)

#define WRN(...) \
    ::util::Logger::getLogger()->log( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::warn, __VA_ARGS__)

#define ERR(...) \
    ::util::Logger::getLogger()->log( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::err, __VA_ARGS__)

#define CRIT(...) \
    ::util::Logger::getLogger()->log( \
        spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, \
        spdlog::level::critical, \
        __VA_ARGS__)
