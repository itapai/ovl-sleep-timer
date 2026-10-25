#include "sleep.hpp"

#include <atomic>

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
        // spsm (ISystemManager)
        constexpr u32 CmdSpsmEnterSleep = 1;

        constexpr u64 FallbackTimeoutNs = 15'000'000'000ULL;

        // AM returns this while it is busy; libnx's appletInitialize retries on it.
        constexpr u32 AmBusyError = 0x19280;

        // Warning dim: brightness setting is multiplied by this, then restored.
        constexpr float DimFactor = 0.2f;
        bool g_dimmed = false;
        float g_savedBrightness = 0.f;

        Mutex &dimMutex() {
            static Mutex mutex = 0;
            return mutex;
        }

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

        // omm is the sysmodule that drives the display/operation-mode side of
        // sleep. On hardware (1.0.4) this turned the screen off briefly and then
        // never returned, presumably waiting on AM, which didn't start the
        // sequence. Kept as an opt-in method. The event is handed over writable
        // so omm can signal it.
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

        // spsm is the power-state manager underneath omm; AM and omm learn about
        // the transition through PSC like for any other sleep.
        Result startSleepViaSpsm() {
            Service *spsm = spsmGetServiceSession();
            if (!serviceIsActive(spsm))
                return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
            return serviceDispatch(spsm, CmdSpsmEnterSleep);
        }

        // The fallback requests can block indefinitely (omm did), so they run on
        // a helper thread and the caller gives up after a timeout. A helper that
        // never returns is left behind; it dies with the overlay process.
        struct Request {
            SleepPath path;
            Result result;
            UEvent done;
        };

        Thread g_helper;
        bool g_helperCreated = false;
        std::atomic<bool> g_helperBusy{false};
        Request g_request;

        void helperEntry(void *) {
            g_request.result = g_request.path == SleepPath::Omm ? startSleepViaOmm() : startSleepViaSpsm();
            g_helperBusy.store(false, std::memory_order_release);
            ueventSignal(&g_request.done);
        }

        Result runWithTimeout(SleepPath path, u64 timeoutNs) {
            if (g_helperBusy.load(std::memory_order_acquire))
                return ResultNoResponse(); // previous request is still stuck

            if (g_helperCreated) {
                threadWaitForExit(&g_helper);
                threadClose(&g_helper);
                g_helperCreated = false;
            }

            g_request.path = path;
            g_request.result = 0;
            ueventCreate(&g_request.done, false);

            Result rc = threadCreate(&g_helper, helperEntry, nullptr, nullptr, 0x2000, 0x2c, -2);
            if (R_FAILED(rc))
                return rc;
            g_helperCreated = true;
            g_helperBusy.store(true, std::memory_order_release);

            rc = threadStart(&g_helper);
            if (R_FAILED(rc)) {
                g_helperBusy.store(false, std::memory_order_release);
                return rc;
            }

            if (R_FAILED(waitSingle(waiterForUEvent(&g_request.done), timeoutNs)))
                return ResultNoResponse();
            return g_request.result;
        }

    }

    Result ResultNoResponse() {
        return MAKERESULT(Module_Libnx, LibnxError_Timeout);
    }

    bool isSupported() {
        return hosversionAtLeast(15, 0, 0);
    }

    Result putSystemToSleep(SleepMethod method, SleepPath *used) {
        SleepPath path = SleepPath::LibnxApplet;
        Result rc = startSleepViaLibnx();

        if (R_FAILED(rc)) {
            path = SleepPath::OverlayProxy;
            rc = startSleepViaOverlayProxy();
        }

        if (R_FAILED(rc)) {
            path = method == SleepMethod::Omm ? SleepPath::Omm : SleepPath::Spsm;
            rc = runWithTimeout(path, FallbackTimeoutNs);
        }

        if (used)
            *used = path;
        return rc;
    }

    void beginDim() {
        Mutex &lock = dimMutex();
        mutexLock(&lock);

        if (!g_dimmed && R_SUCCEEDED(lblInitialize())) {
            float current = 0.f;
            if (R_SUCCEEDED(lblGetCurrentBrightnessSetting(&current)) &&
                R_SUCCEEDED(lblSetCurrentBrightnessSetting(current * DimFactor))) {
                g_savedBrightness = current;
                g_dimmed = true;
            } else {
                lblExit();
            }
        }

        mutexUnlock(&lock);
    }

    void endDim() {
        Mutex &lock = dimMutex();
        mutexLock(&lock);

        if (g_dimmed) {
            lblSetCurrentBrightnessSetting(g_savedBrightness);
            lblExit();
            g_dimmed = false;
        }

        mutexUnlock(&lock);
    }

    const char *pathName(SleepPath path) {
        switch (path) {
            case SleepPath::LibnxApplet:  return "libnx applet";
            case SleepPath::OverlayProxy: return "overlay proxy";
            case SleepPath::Omm:          return "omm";
            case SleepPath::Spsm:         return "spsm";
            default:                      return "none";
        }
    }

}
