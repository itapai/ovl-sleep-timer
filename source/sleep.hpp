// Entering sleep mode. No UI code here.
#pragma once

#include <switch.h>

namespace power {

    // How the last putSystemToSleep() call went.
    struct SleepReport {
        Result result = 0;          // result of the button press
        bool slept = false;         // the console was seen to sleep afterwards
        u64 sleptSeconds = 0;       // how long it slept, if it did
    };

    // True on the firmware this overlay supports.
    bool isSupported();

    // Presses the power button briefly, which makes the system run its normal
    // sleep sequence, then watches for up to ~30 s to see whether the console
    // actually slept. Returns once the console is awake again (or the watch
    // ends) with the outcome in *report.
    Result putSystemToSleep(SleepReport *report);

}
