/**
 * @file worker.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>

/**
 * A single background thread that runs submitted jobs in the order they were submitted.
 */
class Worker {
public:
    Worker();

    /// Stops the thread and joins it once the queue has drained.
    ~Worker();

    Worker(const Worker &) = delete;

    Worker &operator=(const Worker &) = delete;

    Worker(Worker &&) = delete;

    Worker &operator=(Worker &&) = delete;

    /**
     * Queues a job to run on the worker thread.
     * @param job Work to run. Must not outlive whatever it captures.
     */
    void submit(std::function<void()> job);

private:
    /**
     * Runs queued jobs until a stop is requested and the queue has drained.
     * @param stop Stop token of the worker thread.
     */
    void drain(const std::stop_token &stop);

    std::mutex _mutex{};
    std::condition_variable_any _queued{};
    std::deque<std::function<void()> > _jobs{};

    std::jthread _thread;
};