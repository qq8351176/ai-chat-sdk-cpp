#include "../include/util/mylog.h"

#include <iostream>

int main() {
    util::Logger::initLogger("spdlogtest", "stdout", spdlog::level::debug);
    const auto logger = util::Logger::getLogger();
    if (logger == nullptr) {
        std::cerr << "logger is nullptr" << std::endl;
        return 1;
    }

    DBG("debug log: {}", 42);
    INFO("info log: {}", 42);
    return 0;
}
