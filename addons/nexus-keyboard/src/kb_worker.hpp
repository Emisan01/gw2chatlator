// kb_worker.hpp — two-lane worker (interactive typing vs. background/network).
// Included by kb_shared.hpp at its place (it relies on what stands before it there) – include that, not this.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace kb {

enum class Lane {
    Unknown,
    MainUI,
    Interactive,
    Network
};

extern thread_local Lane g_workerLane;

inline Lane CurrentLane() { return g_workerLane; }
inline bool IsInteractiveLane() { return g_workerLane == Lane::Interactive; }

inline void AssertNotInteractiveLane(const char* operation) {
    if (g_workerLane == Lane::Interactive) {
        Log(LOGL_CRITICAL, (std::string("Catenary Invariant violated: ") + operation + " called on Interactive Lane!").c_str());
    }
}

class WorkerLane {
public:
    WorkerLane(const char* name, int priority, size_t maxQueue = 96)
        : name_(name), priority_(priority), maxQueue_(maxQueue) {}

    void Start(Lane lane) {
        stop_ = false;
        thread_ = std::thread([this, lane] { Run(lane); });
    }

    // Pending jobs are dropped; `last` still runs on this lane before it ends.
    void Stop(std::function<void()> last = {}) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            jobs_.clear();
            if (last) jobs_.push_back(std::move(last));
            stop_ = true;
        }
        cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }

    void Post(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (jobs_.size() >= maxQueue_) jobs_.pop_front();  // flood: drop oldest
            jobs_.push_back(std::move(job));
        }
        cv_.notify_one();
    }

    void PostFront(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (jobs_.size() >= maxQueue_) jobs_.pop_back();
            jobs_.push_front(std::move(job));
        }
        cv_.notify_one();
    }

    size_t QueueSize() {
        std::lock_guard<std::mutex> lock(mu_);
        return jobs_.size();
    }

private:
    void Run(Lane lane) {
        g_workerLane = lane;
        SetThreadPriority(GetCurrentThread(), priority_);
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;) {
            std::function<void()> job;
            bool stopping;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
                if (jobs_.empty()) break;
                job = std::move(jobs_.front());
                jobs_.pop_front();
                stopping = stop_;
            }
            if (g_disabled && !stopping) continue;
            try {
                if (!Shielded(job)) Log(LOGL_CRITICAL, "a background job faulted – switched off for this session");
            } catch (...) {
                Log(LOGL_WARNING, "a background job failed");
            }
        }
        CoUninitialize();
        g_workerLane = Lane::Unknown;
    }

    const char* name_;
    int priority_;
    size_t maxQueue_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    bool stop_ = false;
    std::thread thread_;
};

// Two-lane architecture:
//  - Interactive lane: CPU-only, typing suggestions, dropdown choices, instant response (< 1ms).
//  - Network/Background lane: HTTP, translations, WordModel mutations, disk persistence.
class Worker {
public:
    void Start() {
        interactive_.Start(Lane::Interactive);
        network_.Start(Lane::Network);
    }

    // Stop interactive first (drops typing jobs), then network (runs `last` cleanup).
    void Stop(std::function<void()> last = {}) {
        interactive_.Stop();
        network_.Stop(std::move(last));
    }

    // Heavy / blocking work: HTTP, translations, WordModel mutations, disk persistence.
    void Post(std::function<void()> job) {
        network_.Post(std::move(job));
    }

    // Priority / Front of network queue (e.g. user pressed Enter, send immediately).
    void PostFront(std::function<void()> job) {
        network_.PostFront(std::move(job));
    }

    // CPU-only interactive work: ComputeTyping, word bar suggestions (< 1ms).
    void PostInteractive(std::function<void()> job) {
        interactive_.Post(std::move(job));
    }

    size_t QueueSize() {
        return network_.QueueSize();
    }

    size_t InteractiveQueueSize() {
        return interactive_.QueueSize();
    }

private:
    WorkerLane interactive_{"Interactive", THREAD_PRIORITY_ABOVE_NORMAL, 8};
    WorkerLane network_{"Network", THREAD_PRIORITY_BELOW_NORMAL, 96};
};

extern Worker g_worker;

}  // namespace kb
