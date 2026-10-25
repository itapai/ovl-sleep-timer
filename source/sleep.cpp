#include "sleep.hpp"

namespace power {

    namespace {

        // IAllSystemAppletProxiesService (appletAE)
        constexpr u32 CmdOpenOverlayAppletProxy = 300;
        // IOverlayAppletProxy [15.0.0+]
        constexpr u32 CmdGetGlobalStateController = 23;
        // IGlobalStateController
        constexpr u32 CmdStartSleepSequence = 2;

        // omm (IOperationModeManager)
        constexpr u32 CmdEnterSleepAndWait = 4;

        // AM returns this while it is busy; libnx's appletInitialize retries on it.
        constexpr u32 AmBusyError = 0x19280;

        Result startSleepViaLibnx() {
            // libtesla sets __nx_applet_type = AppletType_None, so this session is
            // normally not initialised. Kept in case a host ever provides one.
            if (!serviceIsActive(appletGetServiceSession_GlobalStateController()))
                return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);

            return appletStartSleepSequence(true);
        }

        // Opens IGlobalStateController the way libnx's appletInitialize() does for
        // AppletType_OverlayApplet, calls StartSleepSequence and closes everything.
        // nx-ovlloader is a plain sysmodule (not registered with AM as an applet),
        // so AM may refuse the proxy; the Result says so.
        Result startSleepViaOverlayProxy() {
            Service ae = {}, proxy = {}, globalState = {};

            Result rc = smGetService(&ae, "appletAE");
            if (R_FAILED(rc))
                return rc;

            rc = serviceConvertToDomain(&ae);

            if (R_SUCCEEDED(rc)) {
                for (int attempt = 0; attempt < 20; attempt++) {
                    const u64 reserved = 0;
                    serviceAssumeDomain(&ae);
                    rc = serviceDispatchIn(&ae, CmdOpenOverlayAppletProxy, reserved,
                        .in_send_pid = true,
                        .in_num_handles = 1,
                        .in_handles = { CUR_PROCESS_HANDLE },
                        .out_num_objects = 1,
                        .out_objects = &proxy,
                    );
                    if (R_VALUE(rc) != AmBusyError)
                        break;
                    svcSleepThread(100'000'000ULL);
                }
            }

            if (R_SUCCEEDED(rc)) {
                serviceAssumeDomain(&proxy);
                rc = serviceDispatch(&proxy, CmdGetGlobalStateController,
                    .out_num_objects = 1,
                    .out_objects = &globalState,
                );
            }

            if (R_SUCCEEDED(rc)) {
                const u8 flag = 1; // official software hard-codes true
                serviceAssumeDomain(&globalState);
                rc = serviceDispatchIn(&globalState, CmdStartSleepSequence, flag);
            }

            serviceClose(&globalState);
            serviceClose(&proxy);
            serviceClose(&ae);
            return rc;
        }

        // Fallback for when AM refuses the overlay proxy (seen on hardware:
        // 2128-0980). omm is the sysmodule that carries out the actual sleep
        // transition. The event is handed over writable so omm can signal it;
        // a waiter would get an invalid-handle error rather than block forever.
        Result startSleepViaOmm() {
            Service omm = {};
            Result rc = smGetService(&omm, "omm");
            if (R_FAILED(rc))
                return rc;

            Event event = {};
            rc = eventCreate(&event, false);
            if (R_SUCCEEDED(rc)) {
                rc = serviceDispatch(&omm, CmdEnterSleepAndWait,
                    .in_num_handles = 1,
                    .in_handles = { event.wevent },
                );
                eventClose(&event);
            }

            serviceClose(&omm);
            return rc;
        }

    }

    bool isSupported() {
        return hosversionAtLeast(15, 0, 0);
    }

    Result putSystemToSleep(SleepPath *used) {
        SleepPath path = SleepPath::LibnxApplet;
        Result rc = startSleepViaLibnx();

        if (R_FAILED(rc)) {
            path = SleepPath::OverlayProxy;
            rc = startSleepViaOverlayProxy();
        }

        if (R_FAILED(rc)) {
            path = SleepPath::Omm;
            rc = startSleepViaOmm();
        }

        if (used)
            *used = path;
        return rc;
    }

    void blinkBacklight() {
        if (R_FAILED(lblInitialize()))
            return;

        LblBacklightSwitchStatus status;
        if (R_SUCCEEDED(lblGetBacklightSwitchStatus(&status)) && status == LblBacklightSwitchStatus_Enabled) {
            constexpr u64 fadeNs = 150'000'000ULL;
            if (R_SUCCEEDED(lblSwitchBacklightOff(fadeNs))) {
                svcSleepThread(1'000'000'000ULL);
                lblSwitchBacklightOn(fadeNs);
            }
        }

        lblExit();
    }

    const char *pathName(SleepPath path) {
        switch (path) {
            case SleepPath::LibnxApplet:  return "libnx applet";
            case SleepPath::OverlayProxy: return "overlay proxy";
            case SleepPath::Omm:          return "omm";
            default:                      return "none";
        }
    }

}
