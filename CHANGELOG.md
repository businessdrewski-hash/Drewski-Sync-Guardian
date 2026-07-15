# Changelog

## v0.3.3

- Rebuilt collapsible sections as compact one-line headers.
- Collapsed sections now remove their content height completely.
- Replaced large Show/Hide buttons with small chevrons.
- Reduced UI padding and spacing throughout the dock.

## v0.3.2

- Changed ordinary persistent-drift detection to use estimated corrected output drift whenever Adaptive Soft Sync is active.
- Kept raw transport drift visible in diagnostics while avoiding resets for drift already corrected by Adaptive Soft Sync.
- Reworked Live Diagnostics around a large color-coded `Drift from baseline` line.
- Added an inline `Adaptive Sync corrected to ...` value.
- Moved trend, direction, and baseline to a shorter secondary line.
- Added a show/hide control for technical diagnostic details.
- Made every major dock section collapsible.
- Simplified the Adaptive Soft Sync status wording.

## v0.3.1

- Changed receiver restoration from destructive settings replacement to non-destructive settings update.
- Preserved DistroAV properties plus OBS sync offset, audio tracks, monitoring mode, volume, mute, balance, source flags, and related runtime settings during targeted and full-group rebuilds.
- Applied the same preservation behavior to Restore Saved Settings.

## v0.3.0

- Moved stall detection, reset scheduling, pulse restoration, verification, and automatic recovery to a background watchdog independent of Qt hover/focus events.
- Changed the visible drift and rate refresh cadence to 1000 ms while retaining 250 ms internal sampling.
- Expanded the offset median to 10 seconds and added robust 60-second display and 120-second controller rate estimates.
- Added plain-English drift direction text.
- Increased dark-theme contrast for secondary text and tooltips.
- Reset pulses capture, restore, and verify the original DistroAV configuration, including FrameSync off on audio sources.
- Added experimental Adaptive Soft Sync, disabled by default and fully removable.
- Added a Windows installer workflow producing `SyncGuardian-Setup.exe`, a portable ZIP, and checksums.
