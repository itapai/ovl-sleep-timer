# Changelog

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
