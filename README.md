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

- **Remaining** shows the time left, `Not running`, or an error code.
- **Last sleep** shows how the last timer ended: `slept 42 min` (how long the console
  slept before you woke it), `no sleep seen`, or an error code. Every attempt is also
  appended to `/config/ovl-sleep-timer/sleep.log`.
- **Start** begins the countdown with the duration shown next to it.
- **Cancel** stops it.
- **Duration** presets (30 / 60 / 90 / 120 / 180 min) and **+1 / −1 / +5 / −5 min** set
  the duration Start uses (1–600 min).
- **While a timer is running, the +/− buttons change the time left** instead. They never
  take it below 1 minute or above 600 minutes. Presets still only change the duration for
  the next Start.

While a timer is running, **B hides the overlay** and doesn't close it, so the countdown
keeps going. Press the overlay combo again to see the time left or to cancel.

For the last minute, a notification counts down (`Sleeping in 0:59`, `0:58`, …). The
countdown is silent: the overlay mutes its notification sound until the timer ends.
With the overlay open during that minute, any button cancels the timer, except moving the
cursor and pressing A on an item: **+1 / +5 min** add time instead (a snooze), and
**Cancel** cancels.

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
get the lock screen on wake. The overlay then watches the steady clock for 30 s; a jump
means the console slept, and its size is the time asleep.

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
docker run --rm -v "$PWD:/src" -w /src devkitpro/devkita64 make
```

The output is `ovl-sleep-timer.ovl`, also copied to `out/switch/.overlays/`. CI builds
every push. Pushing a `v*` tag attaches the `.ovl` and an SD-card zip to a GitHub release.

## License

GPL-3.0, see [LICENSE](LICENSE). Uses [libultrahand](https://github.com/ppkantorski/libultrahand)
(GPL-2.0-or-later; its `libultra` part is CC-BY-4.0) as a git submodule.
