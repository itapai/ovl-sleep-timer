#include "sleep.hpp"

namespace power {

    namespace {

        // A short press sleeps; a long one opens the power menu.
        constexpr u64 ButtonPressNs = 150'000'000ULL;
        constexpr u64 ButtonReleaseNs = 100'000'000ULL;

    }

    bool isSupported() {
        return hosversionAtLeast(15, 0, 0);
    }

    // The overlay runs inside nx-ovlloader, which AM doesn't treat as an
    // applet, so it can't call IGlobalStateController::StartSleepSequence
    // (AM answers 2128-0980), and asking spsm or omm directly only blanks
    // the screen for a second. Instead, hid:dbg overrides the sleep-button
    // state that AM watches through hid:sys: AM sees an ordinary short press
    // of the power button and qlaunch runs its own sleep sequence. The
    // override is always removed, or the real button would stop working.
    Result putSystemToSleep() {
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
