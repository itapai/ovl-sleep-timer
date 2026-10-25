// Entering sleep mode and the pre-sleep warning. No UI code here.
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

    // Tries IGlobalStateController::StartSleepSequence(true), the sequence qlaunch
    // runs on a power-button press, then the chosen fallback. Never blocks for
    // more than about 15 s. Returns the Result of the last attempt (or
    // ResultNoResponse()); *used names the path that succeeded or was tried last.
    Result putSystemToSleep(SleepMethod method, SleepPath *used);

    // Returned when the fallback request didn't answer in time.
    Result ResultNoResponse();

    // Drops the screen brightness to a fifth of the current setting without
    // saving it. Best effort; does nothing if already dimmed.
    void beginDim();
    // Restores the brightness saved by beginDim(). Safe to call at any time
    // and from any thread.
    void endDim();

    const char *pathName(SleepPath path);

}
