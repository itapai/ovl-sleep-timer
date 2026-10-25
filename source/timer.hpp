// Countdown that fires a callback at a wall-clock deadline.
//
// UI-free on purpose: it only depends on libnx, so it can be moved into a
// sysmodule later without changes.
#pragma once

#include <switch.h>

#include <atomic>
#include <functional>

class SleepTimer {
public:
    enum class State : u8 {
        Idle,       // never started, or cancelled
        Running,
        Warning,    // inside the warning window before expiry
        Firing,     // expiry callback in progress
        Done,       // expiry callback succeeded
        Failed,     // expiry callback returned an error, see lastResult()
        Missed,     // deadline passed while the console was asleep; nothing done
    };

    struct Callbacks {
        std::function<void()>   onWarning;  // timer thread, once per run
        std::function<Result()> onExpire;   // timer thread, once per run
    };

    SleepTimer() = default;
    ~SleepTimer();

    SleepTimer(const SleepTimer &) = delete;
    SleepTimer &operator=(const SleepTimer &) = delete;

    void setCallbacks(Callbacks callbacks);

    // Starts (or restarts) the countdown. warningSeconds == 0 disables the warning.
    Result start(u64 seconds, u64 warningSeconds);
    // Moves the deadline of a running countdown by deltaSeconds, keeping at least
    // minRemainingSeconds left when shortening. Returns false if not running.
    bool adjust(s64 deltaSeconds, u64 minRemainingSeconds);
    // Stops the countdown and returns to Idle.
    void cancel();
    // Stops and joins the thread. Call before services the callbacks use go away.
    void shutdown();

    State state() const { return m_state.load(std::memory_order_acquire); }
    bool active() const;
    // Seconds until the deadline; 0 when not active.
    u64 remaining() const;
    Result lastResult() const { return m_lastResult.load(std::memory_order_acquire); }

private:
    static void threadEntry(void *arg);
    void run();
    void join();

    // Monotonic seconds. Uses the standard steady clock (keeps counting through
    // sleep) when time is initialised, otherwise the system tick.
    static u64 nowSeconds();

    Thread m_thread = {};
    bool m_threadCreated = false;
    UEvent m_stopEvent = {};

    Callbacks m_callbacks;

    std::atomic<State> m_state{State::Idle};
    std::atomic<u64> m_deadline{0};
    std::atomic<u64> m_warningSeconds{0};
    std::atomic<Result> m_lastResult{0};
};
