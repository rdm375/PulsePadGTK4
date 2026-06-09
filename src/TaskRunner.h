#pragma once

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace pulsepad {

class CancellationToken {
public:
    CancellationToken() = default;
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> cancelled) : cancelled_(std::move(cancelled)) {}

    bool cancellation_requested() const { return cancelled_ && cancelled_->load(); }
    std::shared_ptr<std::atomic<bool>> cancel_flag() const { return cancelled_; }

private:
    std::shared_ptr<std::atomic<bool>> cancelled_;
};

class TaskHandle {
public:
    TaskHandle() = default;
    explicit TaskHandle(std::shared_ptr<std::atomic<bool>> cancelled) : cancelled_(std::move(cancelled)) {}

    void cancel() const {
        if (cancelled_) cancelled_->store(true);
    }

    bool cancellation_requested() const { return cancelled_ && cancelled_->load(); }
    explicit operator bool() const { return static_cast<bool>(cancelled_); }

private:
    std::shared_ptr<std::atomic<bool>> cancelled_;
};

enum class TaskStatus { Success, Failed, Cancelled, Rejected };

template <typename T>
struct TaskOutcome {
    TaskStatus status = TaskStatus::Failed;
    std::string jobName;
    std::string userMessage;
    std::string debugDetail;
    T value{};

    bool succeeded() const { return status == TaskStatus::Success; }
    bool failed() const { return status == TaskStatus::Failed || status == TaskStatus::Rejected; }
    bool cancelled() const { return status == TaskStatus::Cancelled; }

    static TaskOutcome success(std::string name, T result) {
        TaskOutcome out;
        out.status = TaskStatus::Success;
        out.jobName = std::move(name);
        out.value = std::move(result);
        return out;
    }

    static TaskOutcome failure(std::string name, std::string message, std::string detail = {}) {
        TaskOutcome out;
        out.status = TaskStatus::Failed;
        out.jobName = std::move(name);
        out.userMessage = std::move(message);
        out.debugDetail = std::move(detail);
        return out;
    }

    static TaskOutcome cancelled_outcome(std::string name) {
        TaskOutcome out;
        out.status = TaskStatus::Cancelled;
        out.jobName = std::move(name);
        out.userMessage = "Cancelled";
        return out;
    }

    static TaskOutcome rejected(std::string name, std::string message) {
        TaskOutcome out;
        out.status = TaskStatus::Rejected;
        out.jobName = std::move(name);
        out.userMessage = std::move(message);
        return out;
    }
};

class TaskRunner {
public:
    using CompletionExecutor = std::function<void(std::function<void()>)>;

    explicit TaskRunner(std::size_t workerCount = default_worker_count(), std::size_t maxQueuedTasks = 32, CompletionExecutor completionExecutor = {});
    ~TaskRunner();

    TaskRunner(const TaskRunner&) = delete;
    TaskRunner& operator=(const TaskRunner&) = delete;

    TaskRunner(TaskRunner&&) = delete;
    TaskRunner& operator=(TaskRunner&&) = delete;

    static std::size_t default_worker_count();

    template <typename Work, typename Completion>
    TaskHandle submit(std::string jobName, Work&& work, Completion&& completion) {
        using Result = typename std::invoke_result<Work, CancellationToken>::type;
        static_assert(!std::is_void<Result>::value, "TaskRunner work functions must return a result value");

        auto cancelState = std::make_shared<std::atomic<bool>>(false);
        auto handle = TaskHandle(cancelState);
        auto executor = completionExecutor_;
        auto sharedWork = std::make_shared<typename std::decay<Work>::type>(std::forward<Work>(work));
        auto sharedCompletion = std::make_shared<typename std::decay<Completion>::type>(std::forward<Completion>(completion));

        auto postCompletion = [executor, sharedCompletion](TaskOutcome<Result> outcome) mutable {
            auto deliver = [sharedCompletion, outcome = std::move(outcome)]() mutable { (*sharedCompletion)(std::move(outcome)); };
            if (executor) executor(std::move(deliver));
            else deliver();
        };

        Job job;
        job.name = jobName;
        job.cancelState = cancelState;
        job.run = [jobName, cancelState, sharedWork, postCompletion]() mutable {
            if (cancelState->load()) {
                postCompletion(TaskOutcome<Result>::cancelled_outcome(jobName));
                return;
            }

            try {
                Result result = (*sharedWork)(CancellationToken(cancelState));
                if (cancelState->load()) {
                    postCompletion(TaskOutcome<Result>::cancelled_outcome(jobName));
                    return;
                }
                postCompletion(TaskOutcome<Result>::success(jobName, std::move(result)));
            } catch (const std::exception& ex) {
                postCompletion(TaskOutcome<Result>::failure(jobName, ex.what(), ex.what()));
            } catch (...) {
                postCompletion(TaskOutcome<Result>::failure(jobName, "Background task failed", "Unknown exception"));
            }
        };

        if (!enqueue(std::move(job))) {
            cancelState->store(true);
            postCompletion(TaskOutcome<Result>::rejected(jobName, "Too many background tasks queued"));
        }

        return handle;
    }

    void request_stop();
    void shutdown();
    std::size_t queued_count() const;

private:
    struct Job {
        std::string name;
        std::shared_ptr<std::atomic<bool>> cancelState;
        std::function<void()> run;
    };

    bool enqueue(Job job);
    void worker_loop();

    const std::size_t maxQueuedTasks_;
    CompletionExecutor completionExecutor_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Job> jobs_;
    std::vector<std::thread> workers_;
    std::vector<std::weak_ptr<std::atomic<bool>>> taskStates_;
    bool stopping_ = false;
};

} // namespace pulsepad
