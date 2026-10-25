# Changelog

## [1.0.0] - unreleased

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
