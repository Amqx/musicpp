/**
 * @file worker.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include "orchestrator/worker.hpp"

#include <utility>

Worker::Worker() : _thread{[this](std::stop_token stop) { drain(std::move(stop)); }} {
}

Worker::~Worker() = default;

void Worker::submit(std::function<void()> job) { {
        std::lock_guard lock{_mutex};
        _jobs.push_back(std::move(job));
    }
    _queued.notify_one();
}

void Worker::drain(const std::stop_token &stop) {
    while (true) {
        std::function < void() > job; {
            std::unique_lock lock{_mutex};
            _queued.wait(lock, stop, [this] { return !_jobs.empty(); });
            if (_jobs.empty()) {
                return; // Woken by the stop request rather than by a job.
            }
            job = std::move(_jobs.front());
            _jobs.pop_front();
        }
        job();
    }
}
