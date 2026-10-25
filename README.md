# ovl-sleep-timer

Tesla/Ultrahand overlay that puts the Switch to sleep after a set time — for watching movies in handheld.

The built-in auto-sleep only counts idle time, stops at 30 minutes in handheld mode, and
video apps switch it off. This overlay counts down from a duration you pick and then
runs the same sleep sequence as a press of the power button, whatever app is open.

## Requirements

- Atmosphère with nx-ovlloader and either Ultrahand Overlay or Tesla Menu ≥ 1.2.3
- **Firmware 15.0.0 or newer.** On older firmware the overlay only shows a "Requires
  firmware 15.0.0+" message.

## Install

Copy `ovl-sleep-timer.ovl` to `/switch/.overlays/ovl-sleep-timer.ovl` on the SD card,
then reboot or reopen the overlay menu.

## Usage

Open the overlay menu with its button combo (Ultrahand's default is `ZL + ZR + D-Pad Down`,
Tesla's is `L + D-Pad Down + R-Stick`) and choose **Sleep Timer**.

- **Remaining** shows the time left, `Not running`, or an error code.
- **Last sleep** shows how the last sleep attempt went, e.g. `spsm ok, 3 s, wake 0x12`:
  the route, seconds until the console was back, and the wake reason the system reported.
  Every attempt is also appended to `/config/ovl-sleep-timer/sleep.log`.
- **Start** begins the countdown with the duration shown next to it.
- **Cancel** stops it.
- **Duration** presets (30 / 60 / 90 / 120 / 180 min) and **+5 / −5 min** set the
  duration Start uses (5–600 min).
- **While a timer is running, +5 / −5 min change the time left** instead. −5 never takes it
  below 1 minute, and +5 stops at 600 minutes. Presets still only change the duration for the
  next Start.

While a timer is running, **B hides the overlay** and doesn't close it, so the countdown
keeps going. Press the overlay combo again to see the time left or to cancel.

For the last minute, a notification counts down (`Sleeping in 0:59`, `0:58`, …). The
countdown is silent: the overlay mutes its notification sound until the timer ends.
Pressing any button in the open overlay during that minute cancels the timer, except
**+5 min**, which adds 5 minutes instead (a snooze).

If you put the console to sleep yourself and wake it after the deadline, the timer does
nothing and shows `Expired during sleep`. A sleep and wake that happen before the deadline
don't delay it, because the countdown uses the system's steady clock, not a count of ticks.

## Configuration

`/config/ovl-sleep-timer/config.ini` is created the first time you change the duration or
start the timer:

```ini
[settings]
duration_minutes=90
warning=true
```

| Key                | Values         | Meaning                                    |
|--------------------|----------------|--------------------------------------------|
| `duration_minutes` | 5–600          | Last chosen duration, loaded on open       |
| `warning`          | `true`/`false` | Last-minute countdown notification         |
| `sleep_method`     | `button`/`spsm`/`omm` | Fallback sleep route (advanced; never written by the overlay) |

The theme and wallpaper can be overridden in the same folder; see libultrahand's
`UI_OVERRIDE_PATH`.

## Limitations

- **One overlay at a time.** nx-ovlloader runs one overlay at a time. If you open another
  overlay (e.g. Status Monitor) or go back to the Ultrahand menu while the timer is
  running, this overlay is unloaded and the timer is lost. Hide it with B or the combo
  instead. The way around this later is a sysmodule that holds the countdown, with this
  overlay as its front end (`source/timer.*` has no UI code, so it can be moved as-is).
- **Sleep route still being tested on hardware.** See below.

## How sleep is triggered

On expiry the overlay calls `IGlobalStateController::StartSleepSequence(true)` (libnx:
`appletStartSleepSequence`), which is what qlaunch calls when the power button is pressed.
AM exposes that interface to overlay applets on 15.0.0+.

libtesla/libultrahand overlays run with `__nx_applet_type = AppletType_None` inside
nx-ovlloader, which is a regular sysmodule. So libnx never opens an applet session for the
overlay. `source/sleep.cpp` therefore opens one itself, the way libnx's `appletInitialize()`
does for `AppletType_OverlayApplet`:

1. `appletAE` → domain → cmd 300 `OpenOverlayAppletProxy` (sends PID and process handle;
   retries while AM reports busy)
2. `IOverlayAppletProxy` cmd 23 `GetGlobalStateController`
3. `IGlobalStateController` cmd 2 `StartSleepSequence(u8 = 1)`
4. Closes all three sessions.

**AM refuses step 1** on real hardware (1.0.0 showed `2128-0980`, i.e. `0x7A880`), because
nx-ovlloader's process isn't one that AM launched as an applet. The overlay then falls back to
one of these, chosen with `sleep_method` in `config.ini`:

| `sleep_method`   | Request                          | Hardware result so far                              |
|------------------|----------------------------------|-----------------------------------------------------|
| `button` (default) | Simulated short power-button press: `hid:dbg` cmd 121 `SetSleepButtonAutoPilotState` pressed for 150 ms, released, then the override is removed | Not yet tested (1.0.8) |
| `spsm`           | `spsm` cmd 1 `EnterSleep`        | 1.0.6/1.0.7: returns at once (`0 s`); screen dark 1–2 s, video pauses, no sleep (`wake 0x80`) |
| `omm`            | `omm` cmd 4 `EnterSleepAndWait`  | 1.0.4: screen went dark briefly, came back, no sleep; the call never returned |

The fallback request runs on its own thread and is abandoned after 15 seconds. **Remaining**
then shows e.g. `spsm: no response` instead of hanging, and Cancel keeps working. Any other
failure shows the route and code, e.g. `spsm: 2xxx-yyyy (0x…)`. Please open an issue with it.

Left out: the qlaunch general-channel message (message IDs unverified) and
`PerformSystemButtonPressing` (needs a debug permission).

### Test plan

1. Set a 5 min timer (the minimum), hide the overlay, and check that the console sleeps
   at 0:00:00 and the app resumes normally on wake.
2. Repeat while the YouTube app is playing a video.
3. Start a timer, sleep manually, and wake after the deadline: no second sleep, status
   `Expired during sleep`.
4. Start/cancel several times and hide/show several times; then exit the overlay. There
   should be no crash or hang.

## Building

Requires devkitPro (`switch-dev`, plus `switch-curl switch-zlib switch-mbedtls`).

```sh
git clone --recursive https://github.com/itapai/ovl-sleep-timer.git
cd ovl-sleep-timer
make
```

Or with Docker:

```sh
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64 make
```

The output is `ovl-sleep-timer.ovl`, also copied to `out/switch/.overlays/`. CI builds
every push. Pushing a `v*` tag attaches the `.ovl` and an SD-card zip to a GitHub release.

## License

GPL-3.0, see [LICENSE](LICENSE). Uses [libultrahand](https://github.com/ppkantorski/libultrahand)
(GPL-2.0-or-later; its `libultra` part is CC-BY-4.0) as a git submodule.
