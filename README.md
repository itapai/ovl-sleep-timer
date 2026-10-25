# ovl-sleep-timer

Tesla/Ultrahand overlay that puts the Switch to sleep after a set time — for watching movies in handheld.

The built-in auto-sleep only counts idle time, stops at 30 minutes in handheld mode, and
video apps switch it off. This overlay counts down from a duration you pick and then
presses the power button for you, so the console goes to sleep exactly as if you had
pressed it, whatever app is open.

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

```
▌Timer             Sleeps at 23:47
  Stop                 − 1:23:45 +
▌Quick start
  30 min
  1 h
  1 h 30 min
  2 h
  3 h
```

| Control | No timer | Timer running |
|---|---|---|
| **A** on the top row | Start with the duration shown | Stop |
| **◀ / ▶** (−/+) on the top row | Previous / next duration (hold to repeat) | Time left to the previous / next duration |
| **A** on a Quick start row | Start with that time | Restart with that time |
| **B** | Close the overlay | Hide the overlay; the timer keeps going |

- The **Timer** header shows `Off`, `Sleeps at 23:47` (local time), or what went wrong:
  `Sleep failed: 2xxx-yyyy` or `Expired during sleep`.
- −/+ step through these durations, finer for short timers and coarser for long ones:

  | Range | Step | Durations |
  |---|---|---|
  | 1–10 min | 1 min | 1, 2, 3 … 10 min |
  | 10–30 min | 5 min | 15, 20, 25, 30 min |
  | 30 min–2 h | 15 min | 45 min, 1 h, 1 h 15 … 2 h |
  | 2–4 h | 30 min | 2 h 30, 3 h, 3 h 30, 4 h |
  | 4–10 h | 1 h | 5, 6, 7 … 10 h |

- While a timer runs, −/+ round the time left to the next duration down or up, at least 30 s
  away: with 43:27 left, + gives 45:00 and − gives 30:00. The time left never goes below
  1 minute.
- Press the overlay combo again to see the time left or to stop the timer.

For the last minute, a notification counts down (`Sleeping in 0:59`, `0:58`, …). The
countdown is silent: the overlay mutes its notification sound until the timer ends. To snooze,
open the overlay and press **▶** (+, to 2:00); to cancel, press **A** on **Stop**.

If you put the console to sleep yourself and wake it after the deadline, the timer does
nothing and shows `Expired during sleep`. A sleep and wake that happen before the deadline
don't delay it, because the countdown uses the system's steady clock, not a count of ticks.

## Configuration

`/config/ovl-sleep-timer/config.ini` is written when you start a timer or close the overlay
after changing the duration:

```ini
[settings]
duration_minutes=90
warning=true
```

| Key                | Values         | Meaning                                    |
|--------------------|----------------|--------------------------------------------|
| `duration_minutes` | 1–600          | Last chosen duration, loaded on open       |
| `warning`          | `true`/`false` | Last-minute countdown notification         |

The theme and wallpaper can be overridden in the same folder; see libultrahand's
`UI_OVERRIDE_PATH`.

## Limitations

- **One overlay at a time.** nx-ovlloader runs one overlay at a time. If you open another
  overlay (e.g. Status Monitor) or go back to the Ultrahand menu while the timer is
  running, this overlay is unloaded and the timer is lost. Hide it with B or the combo
  instead. The way around this later is a sysmodule that holds the countdown, with this
  overlay as its front end (`source/timer.*` has no UI code, so it can be moved as-is).

## How sleep is triggered

When the timer ends, the overlay presses the power button for 150 ms through the debug HID
service (`hid:dbg` cmd 121 `SetSleepButtonAutoPilotState`), releases it, and removes the
override so the real button keeps working. AM sees an ordinary short press and the HOME
menu runs its normal sleep sequence, so the app is suspended and resumed as usual and you
get the lock screen on wake.

Routes that were tried on hardware and don't work from an overlay:

| Route | Result |
|---|---|
| `IGlobalStateController::StartSleepSequence` via `appletAE` cmd 300 `OpenOverlayAppletProxy` (what the HOME menu itself calls) | AM refuses: `2128-0980`. nx-ovlloader isn't a process AM launched as an applet. |
| `omm` cmd 4 `EnterSleepAndWait` | Screen off for 1–2 s, then back on; the call never returns. |
| `spsm` cmd 1 `EnterSleep` | Returns at once; screen off for 1–2 s, then back on. |

Both direct requests start a sleep that backs out, presumably because AM didn't start it.

## Building

Requires devkitPro (`switch-dev`, plus `switch-curl switch-zlib switch-mbedtls`).

```sh
git clone --recursive https://github.com/itapai/ovl-sleep-timer.git
cd ovl-sleep-timer
make
```

Or with Docker:

```sh
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64:20260219 make
```

The output is `ovl-sleep-timer.ovl`, also copied to `out/switch/.overlays/`. CI builds
every push. Pushing a `v*` tag attaches the `.ovl` and an SD-card zip to a GitHub release.

## License

GPL-3.0, see [LICENSE](LICENSE). Uses [libultrahand](https://github.com/ppkantorski/libultrahand)
(GPL-2.0-or-later; its `libultra` part is CC-BY-4.0) as a git submodule.
