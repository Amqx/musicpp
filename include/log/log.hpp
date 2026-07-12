/**
 * @file log.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 11-Jul-26
 *
 * Level conventions:
 *   trace/debug - poll-loop internals, cache hits, per-poll dumps
 *   info        - track changes, presence updates, auth success
 *   warn        - recoverable source/uploader failures, auth failure/timeout
 *   error       - unexpected failures
 */

#pragma once
#include <spdlog/spdlog.h>
#include <memory>
#include <string_view>

namespace logging {

/**
 * Configures spdlog sinks, patterns, levels, and names.
 */
void init();

/**
 * Returns the named logger, or a default if not registered.
 * @param name Logger name.
 * @return logger.
 */
std::shared_ptr<spdlog::logger> get(const std::string_view &name);

}
