// kb_worker.hpp — the one worker thread (translations, spelling, sending: never on the game's threads).
// Included by kb_shared.hpp at its place (it relies on what stands before it there) – include that, not this.
#pragma once

namespace kb {

// ---------------------------------------------------------------------------------------------------------------
// One worker thread: translations, spelling and sending never run on the game's threads.
class Worker {
public:
    void Start() {
        stop_ = false;
        thread_ = std::thread([this] { Run(); });
    }
    // Pending jobs are dropped; `last` still runs on the worker (it frees what the worker owns) before it ends.
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
            if (jobs_.size() > 96) jobs_.pop_front();  // a flood (WvW): the oldest wait no longer
            jobs_.push_back(std::move(job));
        }
        cv_.notify_one();
    }
    size_t QueueSize() {
        std::lock_guard<std::mutex> lock(mu_);
        return jobs_.size();
    }

private:
    void Run() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);  // language detection, spell checker, word prediction
        for (;;) {
            std::function<void()> job;
            bool stopping;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
                if (jobs_.empty()) break;  // stopped, nothing left
                job = std::move(jobs_.front());
                jobs_.pop_front();
                stopping = stop_;
            }
            if (g_disabled && !stopping) continue;  // switched off by a fault: only the last job (the cleanup) runs
            try {
                if (!Shielded(job)) Log(LOGL_CRITICAL, "a background job faulted – switched off for this session");
            } catch (...) {
                Log(LOGL_WARNING, "a background job failed");
            }
        }
        CoUninitialize();
    }
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> jobs_;
    bool stop_ = false;
    std::thread thread_;
};

extern Worker g_worker;

}  // namespace kb
