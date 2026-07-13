/**
 * @file worker_test.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include "orchestrator/worker.hpp"

using namespace std::chrono_literals;

namespace {
/**
 * Spins until a condition holds, so a test never hangs on a worker that stalls.
 */
template<typename Predicate>
bool waitFor(Predicate done, const std::chrono::milliseconds timeout = 2s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return done();
}
} // namespace

TEST_CASE("A submitted job runs", "[worker]") {
    std::atomic ran{false};

    Worker worker;
    worker.submit([&ran] { ran = true; });

    CHECK(waitFor([&ran] { return ran.load(); }));
}

TEST_CASE("A job runs off the calling thread", "[worker]") {
    std::atomic<std::thread::id> ranOn{};

    Worker worker;
    worker.submit([&ranOn] { ranOn = std::this_thread::get_id(); });

    REQUIRE(waitFor([&ranOn] { return ranOn.load() != std::thread::id{}; }));
    CHECK(ranOn.load() != std::this_thread::get_id());
}

TEST_CASE("Jobs run in the order they were submitted", "[worker]") {
    std::mutex mutex;
    std::vector<int> order;

    {
        Worker worker;
        for (int i = 0; i < 16; ++i) {
            worker.submit([&mutex, &order, i] {
                std::lock_guard lock(mutex);
                order.push_back(i);
            });
        }
    } // The destructor drains the queue before returning.

    std::lock_guard lock(mutex);
    REQUIRE(order.size() == 16);
    for (int i = 0; i < 16; ++i) CHECK(order[i] == i);
}

TEST_CASE("The destructor drains a queue that is still full", "[worker]") {
    std::atomic ran{0};

    {
        Worker worker;
        for (int i = 0; i < 8; ++i) {
            worker.submit([&ran] {
                std::this_thread::sleep_for(2ms);
                ++ran;
            });
        }
    }

    CHECK(ran.load() == 8);
}

TEST_CASE("A worker given nothing to do still shuts down", "[worker]") {
    const Worker worker;
    SUCCEED("Destructing an idle worker does not hang");
}

TEST_CASE("Jobs submitted from many threads all run", "[worker]") {
    std::atomic ran{0};

    {
        Worker worker;
        std::vector<std::jthread> submitters;
        submitters.reserve(4);
        for (int t = 0; t < 4; ++t) {
            submitters.emplace_back([&worker, &ran] {
                for (int i = 0; i < 8; ++i) worker.submit([&ran] { ++ran; });
            });
        }
    }

    CHECK(ran.load() == 32);
}
