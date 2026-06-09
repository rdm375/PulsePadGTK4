#include "TaskRunner.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

using pulsepad::CancellationToken;
using pulsepad::TaskOutcome;
using pulsepad::TaskRunner;
using pulsepad::TaskStatus;

namespace {

struct Waiter {
    std::mutex mutex;
    std::condition_variable cv;
    int count = 0;

    void signal() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++count;
        }
        cv.notify_all();
    }

    bool wait_for_count(int expected) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [&]() { return count >= expected; });
    }
};

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        std::exit(1);
    }
}

void runs_task() {
    Waiter waiter;
    int value = 0;
    TaskRunner runner(1, 4, [](std::function<void()> fn) { fn(); });
    runner.submit("test-run", [](CancellationToken) { return 42; }, [&](TaskOutcome<int> out) {
        expect(out.succeeded(), "task should succeed");
        value = out.value;
        waiter.signal();
    });
    expect(waiter.wait_for_count(1), "completion should run");
    expect(value == 42, "task result should be delivered");
}

void rejects_when_queue_full() {
    Waiter waiter;
    std::atomic<bool> release{false};
    std::vector<TaskStatus> statuses;
    TaskRunner runner(1, 1, [](std::function<void()> fn) { fn(); });

    runner.submit("blocker", [&](CancellationToken) {
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return 1;
    }, [&](TaskOutcome<int> out) {
        statuses.push_back(out.status);
        waiter.signal();
    });

    runner.submit("queued", [](CancellationToken) { return 2; }, [&](TaskOutcome<int> out) {
        statuses.push_back(out.status);
        waiter.signal();
    });

    runner.submit("rejected", [](CancellationToken) { return 3; }, [&](TaskOutcome<int> out) {
        statuses.push_back(out.status);
        waiter.signal();
    });

    release.store(true);
    expect(waiter.wait_for_count(3), "all completions should run");
    bool sawRejected = false;
    for (auto status : statuses) sawRejected = sawRejected || status == TaskStatus::Rejected;
    expect(sawRejected, "queue-full task should be rejected");
}

void cancellation_before_execution() {
    Waiter waiter;
    std::atomic<bool> release{false};
    TaskRunner runner(1, 4, [](std::function<void()> fn) { fn(); });

    runner.submit("blocker", [&](CancellationToken) {
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return 1;
    }, [&](TaskOutcome<int>) { waiter.signal(); });

    auto handle = runner.submit("cancel-before", [](CancellationToken) { return 99; }, [&](TaskOutcome<int> out) {
        expect(out.status == TaskStatus::Cancelled, "queued task should complete as cancelled");
        waiter.signal();
    });
    handle.cancel();
    release.store(true);
    expect(waiter.wait_for_count(2), "cancel-before completions should run");
}

void cancellation_during_execution() {
    Waiter waiter;
    TaskRunner runner(1, 4, [](std::function<void()> fn) { fn(); });
    auto handle = runner.submit("cancel-during", [&](CancellationToken token) {
        while (!token.cancellation_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return 5;
    }, [&](TaskOutcome<int> out) {
        expect(out.status == TaskStatus::Cancelled, "running task should complete as cancelled");
        waiter.signal();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    handle.cancel();
    expect(waiter.wait_for_count(1), "cancel-during completion should run");
}

void shutdown_cancels() {
    Waiter waiter;
    TaskRunner runner(1, 4, [](std::function<void()> fn) { fn(); });
    runner.submit("shutdown", [&](CancellationToken token) {
        while (!token.cancellation_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return 7;
    }, [&](TaskOutcome<int> out) {
        expect(out.status == TaskStatus::Cancelled, "shutdown should cancel running task");
        waiter.signal();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    runner.shutdown();
    expect(waiter.wait_for_count(1), "shutdown cancellation completion should run");
}

} // namespace

int main() {
    runs_task();
    rejects_when_queue_full();
    cancellation_before_execution();
    cancellation_during_execution();
    shutdown_cancels();
    return 0;
}
