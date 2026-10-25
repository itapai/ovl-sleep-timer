// Entering sleep mode and the pre-sleep warning. No UI code here.
#pragma once

#include <switch.h>

namespace power {

    // Which route putSystemToSleep() used, for display in the overlay.
    enum class SleepPath {
        None,
        LibnxApplet,    // libnx applet session (only if the host initialised one)
        OverlayProxy,   // appletAE -> OpenOverlayAppletProxy -> IGlobalStateController
        Omm,            // omm EnterSleepAndWait, used when AM refuses the proxy
    };

    // True if the firmware has IGlobalStateController on the overlay applet proxy.
    bool isSupported();

    // Runs IGlobalStateController::StartSleepSequence(true), the same sequence
    // qlaunch runs on a power-button press. Returns the Result of the last
    // attempt; *used names the path that succeeded (or the last one tried).
    Result putSystemToSleep(SleepPath *used);

    // Drops the screen brightness to a fifth of the current setting without
    // saving it. Best effort; does nothing if already dimmed.
    void beginDim();
    // Restores the brightness saved by beginDim(). Safe to call at any time
    // and from any thread.
    void endDim();

    const char *pathName(SleepPath path);

}
