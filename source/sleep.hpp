// Entering sleep mode. No UI code here.
#pragma once

#include <switch.h>

namespace power {

    // Which route putSystemToSleep() used, for display in the overlay.
    enum class SleepPath {
        None,
        LibnxApplet,    // libnx applet session (only if the host initialised one)
        OverlayProxy,   // appletAE -> OpenOverlayAppletProxy -> IGlobalStateController
        Omm,            // omm EnterSleepAndWait (opt-in fallback)
        Spsm,           // spsm EnterSleep (default fallback)
    };

    // Which fallback to use once AM has refused the overlay proxy.
    enum class SleepMethod {
        Spsm,
        Omm,
    };

    // True if the firmware has IGlobalStateController on the overlay applet proxy.
    bool isSupported();

    // What happened on the last putSystemToSleep() call, for diagnosing why the
    // console comes back on.
    struct SleepReport {
        SleepPath path = SleepPath::None;  // route that succeeded or was tried last
        Result result = 0;
        u64 elapsedSeconds = 0;            // steady clock, so it includes time asleep
        bool hasWakeReason = false;
        u32 wakeReason = 0;                // spsm GetLastWakeReason, raw
    };

    // Tries IGlobalStateController::StartSleepSequence(true), the sequence qlaunch
    // runs on a power-button press, then the chosen fallback. Never blocks for
    // more than about 15 s of awake time. Returns the Result of the last attempt
    // (or ResultNoResponse()) and fills *report if given.
    Result putSystemToSleep(SleepMethod method, SleepReport *report);

    // Returned when the fallback request didn't answer in time.
    Result ResultNoResponse();

    const char *pathName(SleepPath path);

}
