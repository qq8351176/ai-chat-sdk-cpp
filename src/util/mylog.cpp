#include "util/mylog.h"

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace util {
std::shared_ptr<spdlog::logger> Logger::_logger = nullptr;
std::mutex Logger::_mutex;
void Logger::initLogger(
    const std::string& loggerName,
    const std::string& loggerFile,
    spdlog::level::level_enum logLevel) {
    if (_logger == nullptr) {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_logger == nullptr) {
            spdlog::flush_on(logLevel);
            //启用异步日志 将日志信息存放到消息队列 后台现成负责写入文件
            // arg1 队列大小 arg2 后台的线程数量
            spdlog::init_thread_pool(32768, 1);
            if (loggerFile == "stdout") {
                _logger = spdlog::stdout_color_mt(loggerName);
            } else {
                _logger = spdlog::basic_logger_mt<spdlog::async_factory>(loggerName, loggerFile);
            }
        }
        _logger->set_pattern("[%H:%M:%S][%-7l][%n][%s:%#] %v");
        _logger->set_level(logLevel);
    }
}

std::shared_ptr<spdlog::logger> Logger::getLogger() { return _logger; }

} // namespace util
