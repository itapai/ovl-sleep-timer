#include "timer.hpp"

namespace {

    constexpr u64 TickNs = 1'000'000'000ULL;
    // A tick that took longer than this means the thread was suspended,
    // i.e. the console was asleep in between.
    constexpr u64 SuspendGapSeconds = 5;
    constexpr size_t ThreadStackSize = 0x4000;
    constexpr int ThreadPriority = 0x2c;

}

SleepTimer::~SleepTimer() {
    shutdown();
}

void SleepTimer::setCallbacks(Callbacks callbacks) {
    join();
    m_callbacks = std::move(callbacks);
}

u64 SleepTimer::nowSeconds() {
    TimeSteadyClockTimePoint point;
    if (serviceIsActive(timeGetServiceSession_SteadyClock()) &&
        R_SUCCEEDED(timeGetStandardSteadyClockTimePoint(&point)) && point.time_point >= 0)
        return static_cast<u64>(point.time_point);

    return armTicksToNs(armGetSystemTick()) / TickNs;
}

bool SleepTimer::active() const {
    const State s = state();
    return s == State::Running || s == State::Warning || s == State::Firing;
}

u64 SleepTimer::remaining() const {
    if (!active())
        return 0;
    const u64 now = nowSeconds();
    const u64 deadline = m_deadline.load(std::memory_order_acquire);
    return deadline > now ? deadline - now : 0;
}

Result SleepTimer::start(u64 seconds, u64 warningSeconds) {
    join();

    m_deadline.store(nowSeconds() + seconds, std::memory_order_release);
    m_warningSeconds.store(warningSeconds < seconds ? warningSeconds : 0, std::memory_order_release);
    m_lastResult.store(0, std::memory_order_release);
    ueventCreate(&m_stopEvent, false);

    Result rc = threadCreate(&m_thread, threadEntry, this, nullptr, ThreadStackSize, ThreadPriority, -2);
    if (R_FAILED(rc)) {
        m_lastResult.store(rc, std::memory_order_release);
        m_state.store(State::Failed, std::memory_order_release);
        return rc;
    }
    m_threadCreated = true;

    m_state.store(State::Running, std::memory_order_release);
    rc = threadStart(&m_thread);
    if (R_FAILED(rc)) {
        threadClose(&m_thread);
        m_threadCreated = false;
        m_lastResult.store(rc, std::memory_order_release);
        m_state.store(State::Failed, std::memory_order_release);
    }
    return rc;
}

bool SleepTimer::adjust(s64 deltaSeconds, u64 minRemainingSeconds) {
    if (state() != State::Running && state() != State::Warning)
        return false;

    const u64 now = nowSeconds();
    u64 deadline = m_deadline.load(std::memory_order_acquire);
    u64 updated;
    do {
        const s64 target = s64(deadline) + deltaSeconds;
        const u64 floor = now + minRemainingSeconds;
        // Shortening never goes below the floor, and never lengthens the timer
        // when it is already under it.
        if (deltaSeconds < 0)
            updated = target < s64(floor) ? (deadline < floor ? deadline : floor) : u64(target);
        else
            updated = u64(target);
    } while (!m_deadline.compare_exchange_weak(deadline, updated, std::memory_order_acq_rel));

    return true;
}

void SleepTimer::cancel() {
    join();
    m_state.store(State::Idle, std::memory_order_release);
}

void SleepTimer::shutdown() {
    join();
}

void SleepTimer::join() {
    if (!m_threadCreated)
        return;

    ueventSignal(&m_stopEvent);
    threadWaitForExit(&m_thread);
    threadClose(&m_thread);
    m_threadCreated = false;
}

void SleepTimer::threadEntry(void *arg) {
    static_cast<SleepTimer *>(arg)->run();
}

void SleepTimer::run() {
    // Deadline and "last seen" are both wall-clock, so a manual sleep/wake in the
    // middle of the countdown neither delays nor fast-forwards it.
    u64 lastSeen = nowSeconds();
    bool warned = false;

    while (true) {
        // Signalled stop event: cancel() or shutdown(). Timeout: one tick.
        if (R_SUCCEEDED(waitSingle(waiterForUEvent(&m_stopEvent), TickNs)))
            return;

        const u64 now = nowSeconds();
        const bool wasSuspended = now > lastSeen + SuspendGapSeconds;
        lastSeen = now;

        const u64 deadline = m_deadline.load(std::memory_order_acquire);

        if (now >= deadline) {
            if (wasSuspended) {
                // Deadline passed while asleep: the user woke the console on
                // purpose, so don't send it straight back to sleep.
                m_state.store(State::Missed, std::memory_order_release);
                return;
            }

            m_state.store(State::Firing, std::memory_order_release);
            const Result rc = m_callbacks.onExpire ? m_callbacks.onExpire() : 0;
            m_lastResult.store(rc, std::memory_order_release);
            m_state.store(R_SUCCEEDED(rc) ? State::Done : State::Failed, std::memory_order_release);
            return;
        }

        // Re-evaluated every tick because adjust() can move the deadline in or
        // out of the warning window.
        const u64 warningSeconds = m_warningSeconds.load(std::memory_order_acquire);
        const bool inWindow = warningSeconds != 0 && deadline - now <= warningSeconds;
        if (inWindow && !warned) {
            warned = true;
            m_state.store(State::Warning, std::memory_order_release);
            if (m_callbacks.onWarning)
                m_callbacks.onWarning();
        } else if (!inWindow && warned) {
            warned = false;
            m_state.store(State::Running, std::memory_order_release);
        }

        if (inWindow && m_callbacks.onWarningTick)
            m_callbacks.onWarningTick(deadline - now);
    }
}
