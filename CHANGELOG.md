# Changelog

## [1.2.0] - 2026-10-25

### Changed
- Simpler controls. One **Start / Stop** row replaces Remaining, Start, Cancel and the four
  ± rows: A starts or stops, ◀ / ▶ change the duration (or the time left while running) by
  5 min, and holding repeats. Durations snap to 1, 5, 10, 15 … 600 min.
- **Quick start** presets (30 min … 3 h) start the timer right away instead of only setting
  the duration; while a timer runs they restart it with that time.
- The **Timer** header shows when the console will sleep (`Sleeps at 23:47`), or the error.
- In the last minute, only **Stop** cancels the timer. Other buttons no longer cancel it.
- `config.ini` is written when a timer starts or the overlay closes, not on every press.
- CI builds with a pinned devkitPro image (`devkita64:20260219`).

### Fixed
- **B** in the last minute cancelled the timer instead of hiding the overlay.

### Removed
- **Last sleep**, `sleep.log`, and the 30 s sleep check behind them (debug features). Stopping
  the timer or closing the overlay while it said `Sleeping…` could freeze it for up to 30 s.
- The ±1 min buttons; 1 min is still available as the shortest duration.

## [1.1.1] - 2026-10-25

### Fixed
- With the overlay open, the timer cancelled itself as soon as the last minute began.
  libtesla calls item listeners every frame even with no button pressed, and each listener
  cancelled during the warning. Listeners now act only on A; other buttons still cancel.
  Moving the cursor no longer cancels.

### Added
- **+1 / −1 min** buttons next to +5 / −5. The minimum duration is now 1 minute. A timer
  shorter than a minute gets the countdown for all but its first second.

## [1.1.0] - 2026-10-25

First version confirmed on hardware to put the console to sleep.

### Changed
- Sleep works by simulating a short power-button press (`hid:dbg`), so the HOME menu runs
  its normal sleep sequence. Confirmed on hardware: the screen stays off until the power
  button is pressed, and the console wakes to the lock screen.
- **Last sleep** now shows how long the console actually slept (`slept 42 min`), or
  `no sleep seen`, measured on the steady clock after the press. `sleep.log` records the same.

### Removed
- The routes that didn't work on hardware (AM overlay proxy, `spsm`, `omm`), the
  `sleep_method` setting, the 15 s request timeout they needed, and the wake-reason readout.

## [1.0.8] - 2026-10-25

### Changed
- New default sleep route `button`: a simulated short press of the power button through
  `hid:dbg`, so AM and the HOME menu run their own sleep sequence. On hardware, `spsm`
  returned at once and backed out of sleep after 1–2 s; it stays available as
  `sleep_method=spsm`, as does `omm`.
- After a button press the overlay waits 5 s before recording **Last sleep**, so a real
  sleep shows up as a long time there.

## [1.0.7] - 2026-10-25

### Added
- Sleep diagnostics: a **Last sleep** line shows the route, how long until the console was
  back (steady clock, so time asleep counts) and spsm's last wake reason. Each attempt is
  also appended to `/config/ovl-sleep-timer/sleep.log`.

### Removed
- The one-second dim at the start of the last minute (not noticeable enough to be useful).

## [1.0.6] - 2026-10-25

### Changed
- Sleep fallback is now `spsm` `EnterSleep`. The `omm` route from 1.0.1 dimmed the screen
  briefly on hardware but never slept or returned; it can still be chosen with
  `sleep_method=omm` in `config.ini`.

### Fixed
- A sleep request that never answers no longer leaves the overlay stuck on `Sleeping…`
  (and would have frozen Cancel). It is abandoned after 15 s and shows `<route>: no response`.

## [1.0.5] - 2026-10-25

### Fixed
- The last-minute countdown played the notification sound every second. Notifications are
  now silent from the start of the countdown until the timer ends.

## [1.0.4] - 2026-10-25

### Changed
- The warning dims the screen for a second (to a fifth of the current brightness) instead
  of switching the backlight off.

### Added
- Countdown notification for the last minute (`Sleeping in 0:59`, `0:58`, …), updated in
  place every second.

## [1.0.3] - 2026-10-25

### Changed
- +5 / −5 min moved up, directly under Start and Cancel; presets follow.

## [1.0.2] - 2026-10-25

### Added
- +5 / −5 min adjust the time left while a timer is running (never below 1 min left,
  never above 600 min). +5 during the final-minute warning works as a snooze.

## [1.0.1] - 2026-10-25

### Fixed
- Console did not sleep: AM refuses the overlay applet proxy (`2128-0980`). Sleep now
  falls back to `omm` `EnterSleepAndWait`.

### Changed
- Duration adjust step and minimum are now 5 min (was 15).
- A sleep error now shows which route failed, e.g. `omm: 2xxx-yyyy (0x…)`.

## [1.0.0] - 2026-10-25

### Added
- Countdown overlay with 30/60/90/120/180 min presets, ±15 min adjust, Start and Cancel.
- Last chosen duration persisted in `/config/ovl-sleep-timer/config.ini`.
- Wall-clock countdown on the standard steady clock; a deadline that passes while the
  console is asleep is skipped, not fired on wake.
- Sleep via `IGlobalStateController::StartSleepSequence(true)`, opened through
  `OpenOverlayAppletProxy`; error code shown in the overlay on failure.
- One-minute warning: notification plus a one-second backlight blink; any input in the
  overlay cancels.
- B hides the overlay instead of closing it while a timer is running.
- GitHub Actions build; tagged `v*` pushes attach the `.ovl` to a release.
