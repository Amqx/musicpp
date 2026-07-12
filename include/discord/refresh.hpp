/**
 * @file refresh.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#pragma once

#include <atomic>
#include <chrono>
#include <thread>
#include <discordpp.h>

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
    std::thread loopThread;
    std::atomic<bool> running{false};
    const std::chrono::milliseconds kDiscordRefreshInterval = std::chrono::milliseconds(10);

    void loop() const {
        while (running.load(std::memory_order_relaxed)) {
            try {
                discordpp::RunCallbacks();
            } catch (const std::exception &e) {
                (void)e;
            }

            std::this_thread::sleep_for(kDiscordRefreshInterval);
        }
    }

    DiscordRefresher() : loopThread(&DiscordRefresher::loop, this), running(true) {
    }

    ~DiscordRefresher() {
        running.store(false, std::memory_order_relaxed);
        if (loopThread.joinable())
            loopThread.join();
    }
};