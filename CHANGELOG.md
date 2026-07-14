# Changelog

## 0.2.0 — 2026-07-14

- Added Observe, Ask, and Fully Automatic operating modes.
- Added a five-second rolling-median video-versus-desktop-audio timestamp estimate.
- Added 30-second stable automatic baseline calibration.
- Added persistent A/V drift and drift-rate monitoring.
- Added video, desktop-audio, and microphone packet-stall detection.
- Added confidence scoring that uses recent timestamp discontinuities only as supporting evidence.
- Added direction-aware targeted recovery for video-behind and audio-behind cases.
- Added recovery verification and one optional full-group escalation.
- Added startup/scene-change grace, output-active gating, source-active gating, cooldowns, and hourly reset limits.
- Added 30-second pre-event and post-event JSON incident reports.
- Added explicit timestamp-discontinuity event logging.
- Added a hotkey for restarting A/V auto calibration.
- Preserved all Version 1 manual controls, snapshots, event logs, chapter markers, and exact-setting restoration.

## 0.1.0 — 2026-07-14

- Added an OBS dock for mapping the NDI video, desktop-audio, and microphone receiver sources.
- Added manual per-source and whole-group DistroAV receiver rebuild controls.
- Added frontend hotkeys for every recovery action.
- Added complete source-setting snapshots and restore/rebuild support.
- Added audio/video timestamp monitoring, packet-age monitoring, jump counters, A/V offset estimates, and OBS performance counters.
- Added recording chapter markers and JSONL event logging.
- Added reset overlap protection and guaranteed settings restoration when an active reset is interrupted by plugin unload.
