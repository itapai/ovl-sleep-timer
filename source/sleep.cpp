#include "sleep.hpp"

namespace power {

    namespace {

        // A short press sleeps; a long one opens the power menu.
        constexpr u64 ButtonPressNs = 150'000'000ULL;
        constexpr u64 ButtonReleaseNs = 100'000'000ULL;

        // After the press, how long to watch for the sleep, and how big a jump
        // in the steady clock between one-second polls counts as "was asleep".
        constexpr u64 WatchSeconds = 30;
        constexpr u64 SuspendGapSeconds = 5;
        constexpr u64 PollNs = 1'000'000'000ULL;

        // The standard steady clock keeps counting while the console sleeps.
        u64 steadySeconds() {
            TimeSteadyClockTimePoint point;
            if (serviceIsActive(timeGetServiceSession_SteadyClock()) &&
                R_SUCCEEDED(timeGetStandardSteadyClockTimePoint(&point)) && point.time_point >= 0)
                return static_cast<u64>(point.time_point);
            return armTicksToNs(armGetSystemTick()) / 1'000'000'000ULL;
        }

        // The overlay runs inside nx-ovlloader, which AM doesn't treat as an
        // applet, so it can't call IGlobalStateController::StartSleepSequence
        // (AM answers 2128-0980), and asking spsm or omm directly only blanks
        // the screen for a second. Instead, hid:dbg overrides the sleep-button
        // state that AM watches through hid:sys: AM sees an ordinary short press
        // of the power button and qlaunch runs its own sleep sequence. The
        // override is always removed, or the real button would stop working.
        Result pressPowerButton() {
            Result rc = hiddbgInitialize();
            if (R_FAILED(rc))
                return rc;

            HiddbgSleepButtonAutoPilotState state = { .buttons = 1 };
            rc = hiddbgSetSleepButtonAutoPilotState(&state);
            if (R_SUCCEEDED(rc)) {
                svcSleepThread(ButtonPressNs);
                state.buttons = 0;
                hiddbgSetSleepButtonAutoPilotState(&state);
                svcSleepThread(ButtonReleaseNs);
            }
            hiddbgUnsetSleepButtonAutoPilotState();

            hiddbgExit();
            return rc;
        }

    }

    bool isSupported() {
        return hosversionAtLeast(15, 0, 0);
    }

    Result putSystemToSleep(SleepReport *report) {
        SleepReport result;
        result.result = pressPowerButton();

        if (R_SUCCEEDED(result.result)) {
            // This thread is suspended along with everything else while the
            // console sleeps, so the sleep shows up as one large jump.
            u64 last = steadySeconds();
            const u64 until = last + WatchSeconds;
            while (last < until) {
                svcSleepThread(PollNs);
                const u64 now = steadySeconds();
                if (now > last + SuspendGapSeconds) {
                    result.slept = true;
                    result.sleptSeconds = now - last;
                    break;
                }
                last = now;
            }
        }

        if (report)
            *report = result;
        return result.result;
    }

}
