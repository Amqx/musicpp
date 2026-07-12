/**
 * @file log.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 11-Jul-26
 */

#include "log/log.hpp"

#include <chrono>
#include <filesystem>
#include <string>

#include "log/logGlobal.hpp"

namespace logging {

void init() {
    Logging::initialize();
}

std::shared_ptr<spdlog::logger> get(const std::string_view &name) {
    if (auto logger = spdlog::get(std::string(name))) {
        return logger;
    }
    return spdlog::default_logger();
}

}
