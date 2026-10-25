// Entering sleep mode. No UI code here.
#pragma once

#include <switch.h>

namespace power {

    // True on the firmware this overlay supports.
    bool isSupported();

    // Presses the power button briefly, which makes the system run its normal
    // sleep sequence. Returns once the button is released again.
    Result putSystemToSleep();

}
