/**
 * @file refresh.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <discordpp.h>

#include "log/log.hpp"

class DiscordRefresher {
public:
    static void initialize() {
        static DiscordRefresher refresher;
    }

    DiscordRefresher(const DiscordRefresher &) = delete;

    DiscordRefresher &operator=(const DiscordRefresher &) = delete;

    DiscordRefresher(DiscordRefresher &&) = delete;

    DiscordRefresher &operator=(DiscordRefresher &&) = delete;

private:
    static constexpr std::chrono::milliseconds kDiscordRefreshInterval{10};

    static constexpr unsigned kFailureLogInterval = 500;
    static constexpr unsigned kFailureEscalation = 3000;

    std::atomic<bool> running{true};
    std::thread loopThread;

    /**
     * Logs a RunCallbacks() failure, throttled and escalating with the consecutive-failure count.
     * @param logger Discord logger.
     * @param consecutiveFailures Failures in the current unbroken run.
     * @param what Exception text.
     */
    static void reportFailure(const std::shared_ptr<spdlog::logger> &logger,
                              const unsigned consecutiveFailures, const char *what) {
        if (consecutiveFailures != 1 && consecutiveFailures % kFailureLogInterval != 0)
            return;

        if (consecutiveFailures >= kFailureEscalation) {
            logger->critical("discordpp::RunCallbacks() has failed {} times in a row; rich "
                             "presence is no longer updating: {}", consecutiveFailures, what);
        } else {
            logger->error("discordpp::RunCallbacks() failed ({} in a row): {}",
                          consecutiveFailures, what);
        }
    }

    void loop() const {
        const auto &logger = logging::get("discord");
        unsigned consecutiveFailures = 0;

        while (running.load(std::memory_order_relaxed)) {
            try {
                discordpp::RunCallbacks();
                if (consecutiveFailures > 0) {
                    logger->info("discordpp::RunCallbacks() recovered after {} failure(s)",
                                 consecutiveFailures);
                    consecutiveFailures = 0;
                }
            } catch (const std::exception &e) {
                reportFailure(logger, ++consecutiveFailures, e.what());
            } catch (...) {
                reportFailure(logger, ++consecutiveFailures, "unknown exception");
            }

            std::this_thread::sleep_for(kDiscordRefreshInterval);
        }
    }

    DiscordRefresher() : loopThread(&DiscordRefresher::loop, this) {
    }

    ~DiscordRefresher() {
        running.store(false, std::memory_order_relaxed);
        if (loopThread.joinable())
            loopThread.join();
    }
};