#include "TaskRunner.h"

#include <algorithm>
#include <condition_variable>

namespace pulsepad {

std::size_t TaskRunner::default_worker_count() {
    const auto n = std::thread::hardware_concurrency();
    if (n == 0) return 2;
    return std::max<std::size_t>(2, std::min<std::size_t>(4, n));
}

TaskRunner::TaskRunner(std::size_t workerCount, std::size_t maxQueuedTasks, CompletionExecutor completionExecutor)
    : maxQueuedTasks_(std::max<std::size_t>(1, maxQueuedTasks)), completionExecutor_(std::move(completionExecutor)) {
    workerCount = std::max<std::size_t>(1, workerCount);
    workers_.reserve(workerCount);
    for (std::size_t i = 0; i < workerCount; ++i) workers_.emplace_back([this]() { worker_loop(); });
}

TaskRunner::~TaskRunner() { shutdown(); }

bool TaskRunner::enqueue(Job job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        if (jobs_.size() >= maxQueuedTasks_) return false;
        taskStates_.erase(std::remove_if(taskStates_.begin(), taskStates_.end(), [](const auto& weak) { return weak.expired(); }), taskStates_.end());
        taskStates_.push_back(job.cancelState);
        jobs_.push(std::move(job));
    }
    cv_.notify_one();
    return true;
}

void TaskRunner::request_stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        for (auto& weak : taskStates_) {
            if (auto state = weak.lock()) state->store(true);
        }
    }
    cv_.notify_all();
}

void TaskRunner::shutdown() {
    request_stop();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

std::size_t TaskRunner::queued_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_.size();
}

void TaskRunner::worker_loop() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) return;
            job = std::move(jobs_.front());
            jobs_.pop();
        }
        if (job.run) job.run();
    }
}

} // namespace pulsepad
