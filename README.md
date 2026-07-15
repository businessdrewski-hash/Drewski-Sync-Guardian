# Sync Guardian for OBS Studio

Sync Guardian is an experimental OBS Studio plugin for people dealing with **NDI / DistroAV audio-video sync problems** on a receiving or streaming PC.

It monitors selected NDI sources, tracks gradual A/V drift, detects timestamp stalls and receiver failures, and can recover individual sources or rebuild an entire sync group while preserving the source settings you already configured.

It is designed for setups where video, desktop audio, and microphone audio may arrive through separate NDI sources and slowly move out of alignment over time.

> Install Sync Guardian on the **receiving PC** where the DistroAV sources exist in OBS.

## What problems it is designed to help with

Sync Guardian may be useful when an NDI-based OBS setup experiences problems such as:

- Video gradually drifting behind or ahead of desktop audio
- A/V sync changing over a long stream or recording
- An NDI video source freezing while audio continues
- A source reconnecting with the wrong timing relationship
- OBS or DistroAV sources failing to recover until the interface is interacted with
- Manual receiver resets changing or wiping source properties
- Separate NDI audio sources needing to remain aligned with video

Sync Guardian does not replace DistroAV or NDI. It watches the sources already present in OBS and adds monitoring, controlled recovery, diagnostics, and optional timing correction.

## Main features

### Live A/V drift monitoring

Sync Guardian compares the selected video and desktop-audio timestamps and establishes a session baseline.

The main diagnostic line shows:

- Current drift from baseline
- Whether video is gradually rushing or dragging
- Estimated drift rate in milliseconds per minute
- Raw transport drift
- Estimated corrected drift when Adaptive Soft Sync is active

The display refreshes once per second while timestamps continue to be sampled internally at a higher rate. Median filtering and longer trend windows reduce noisy rate changes.

### Clear, color-coded diagnostics

The Live Diagnostics area emphasizes the most important value: **Drift from baseline**.

- Green: near the current sync target
- Amber: moving into a concerning range
- Red: beyond the configured persistent-drift threshold

Technical details such as packet age, raw timestamps, jump counters, confidence, and OBS statistics can be shown only when needed.

### Background watchdog

Detection and recovery run independently of the dock's paint and mouse events.

This means automatic recovery should continue even when:

- OBS is unfocused
- The dock is collapsed
- The mouse is not moving over the interface
- The UI is temporarily idle

The watchdog handles stall detection, sustained-drift timing, reset pulse completion, verification, escalation, and cooldowns.

### Targeted NDI receiver reset

A single mapped source can be reset without restarting OBS.

Sync Guardian briefly changes a DistroAV receiver property to force receiver recreation, then restores the complete captured configuration. Reset pulses preserve the original FrameSync state rather than automatically enabling it.

### Full sync-group rebuild

Video, desktop audio, and microphone sources can be rebuilt together when a single-source reset is not enough.

Before the rebuild, Sync Guardian captures and restores:

- DistroAV source selection and receiver properties
- FrameSync state
- Timing and latency settings
- OBS sync offset
- Audio-track routing
- Monitoring mode
- Volume, mute, and balance
- Push-to-talk and push-to-mute settings
- Enabled state and source flags
- Deinterlacing settings

The restore path is non-destructive, so properties not explicitly understood by Sync Guardian are not intentionally erased.

### Automatic recovery modes

Sync Guardian supports multiple operating modes:

- **Observe only** — display and log problems without changing sources
- **Ask before recovery** — request confirmation before taking action
- **Fully automatic** — perform targeted recovery when configured conditions persist

Safety controls include startup grace periods, scene-change grace periods, reset cooldowns, verification, escalation limits, and maximum automatic resets per hour.

### Persistent drift recovery

A configurable persistent-drift threshold can trigger recovery when A/V drift remains outside the allowed range long enough.

Threshold behavior depends on Adaptive Soft Sync:

- **Adaptive Soft Sync off:** the threshold uses raw transport drift
- **Adaptive Soft Sync active:** the threshold uses estimated corrected output drift

Example:

```text
Raw drift:                 -50 ms
Adaptive correction:       +48 ms
Estimated corrected drift:  -2 ms
```

With a 50 ms threshold, that corrected result does not trigger ordinary persistent-drift recovery.

### Adaptive Soft Sync

Adaptive Soft Sync is an **experimental, optional audio-resampling system** intended to counter slow, steady drift without using large audible cuts.

When enabled, Sync Guardian attaches a private audio filter to the mapped desktop-audio source and applies a very small correction measured in parts per million.

A typical drift of about `-1.5 ms/min` requires only approximately `25 ppm` of correction. The default limit is conservative, and the controller changes correction gradually rather than reacting to every short-term fluctuation.

Adaptive Soft Sync can:

- Slow or speed the effective audio timeline by tiny amounts
- Hold corrected output closer to the calibrated baseline
- Avoid treating already-corrected drift as an ordinary threshold violation
- Optionally apply linked correction to the microphone source
- Recenter after a receiver reset

It can also be completely disabled. Clearing **Enable Adaptive Soft Sync** or pressing **Disable and remove Soft Sync filters** removes Sync Guardian's private filters and returns audio to normal pass-through.

### Event history and diagnostics export

Sync Guardian records important events such as:

- Timestamp jumps
- Source stalls
- Persistent drift episodes
- Reset attempts
- Verification results
- Escalation and cooldown states

The dock is arranged into collapsible sections so routine use can remain compact while detailed information stays available for troubleshooting.

## Recommended first-time setup

1. Install the plugin on the receiving or streaming PC.
2. Open OBS and select **Docks → Sync Guardian**.
3. Map the NDI video, desktop-audio, and optional microphone sources.
4. Begin in **Observe only** mode.
5. Let the system establish a baseline and run long enough to understand the normal drift pattern.
6. Configure conservative thresholds before enabling automatic recovery.
7. Leave Adaptive Soft Sync disabled until ordinary monitoring and reset behavior are confirmed stable.

For split-source setups, audio FrameSync behavior should be tested carefully. Some systems are more stable with FrameSync disabled on audio-only NDI sources.

## Installation

### Windows installer

Download the Windows build artifact from GitHub Actions and run:

```text
SyncGuardian-Setup.exe
```

Close OBS before installing. The default OBS location is:

```text
C:\Program Files\obs-studio\
```

The installer adds normal Windows uninstall support.

### Portable installation

Extract `SyncGuardian-Portable.zip`, then copy its `obs-plugins` and `data` folders into:

```text
C:\Program Files\obs-studio\
```

The plugin DLL should end up at:

```text
C:\Program Files\obs-studio\obs-plugins\64bit\sync-guardian.dll
```

Start OBS and open:

```text
Docks → Sync Guardian
```

## Building from GitHub

Open the repository's **Actions** tab and run:

```text
Build Sync Guardian for Windows
```

The generated artifact contains:

- `SyncGuardian-Setup.exe`
- `SyncGuardian-Portable.zip`
- `SHA256SUMS.txt`

The source package itself does not include a precompiled Windows DLL.

## Basic version history

### v0.1.x — Manual recovery foundation

- Added an OBS dock for mapped DistroAV sources
- Added manual targeted receiver resets
- Added basic saved-state restoration
- Established the reset-pulse approach for receiver recreation

### v0.2.0 — Automatic monitoring and recovery

- Added automatic stall and drift detection
- Added Observe, Ask, and automatic operating modes
- Added cooldowns, startup grace periods, verification, and escalation
- Added event logging and persistent-drift handling

### v0.2.1 — Dock usability

- Added scrolling and improved dock behavior at smaller sizes
- Made the interface more practical for normal OBS layouts

### v0.2.2 — Visual cleanup

- Added clearer button states and highlighting
- Reduced unnecessary visual density
- Improved general layout and control grouping

### v0.2.3 — Packaging and diagnostics

- Added a cleaner drag-and-drop OBS package structure
- Added more understandable diagnostic summaries
- Improved distribution for GitHub Actions builds

### v0.2.4 — Reliable video timestamp probe

- Replaced unreliable video-frame polling with a private pass-through async video filter
- Added separate frame-arrival and timestamp-advancement tracking
- Prevented false `inf` packet-age stalls before valid timestamps were available
- Added probe-ready and waiting states
- Improved startup settling and source validation

### v0.3.0 — Background watchdog and Adaptive Soft Sync

- Moved safety-critical monitoring and reset timing away from Qt hover/focus behavior
- Added 1000 ms display refresh with 250 ms internal sampling
- Expanded median filtering and robust trend estimation
- Added plain-English direction descriptions
- Improved dark-theme text contrast
- Preserved original FrameSync and DistroAV settings during reset pulses
- Added experimental Adaptive Soft Sync
- Added installer and portable-package generation

### v0.3.1 — Non-destructive settings preservation

- Replaced destructive source-setting restoration with non-destructive updates
- Preserved DistroAV and OBS-level source properties during targeted resets and full-group rebuilds
- Applied the same preservation behavior to saved-state restoration

### v0.3.3 — Compact collapsible interface

- True single-line collapsed headers
- Small chevron toggles instead of large Show/Hide buttons
- Reduced vertical padding and section spacing

### v0.3.2 — Corrected-drift logic and compact diagnostics

- Changed persistent-drift evaluation to use corrected output drift while Adaptive Soft Sync is active
- Kept raw transport drift available for diagnosis
- Added a prominent color-coded drift line
- Added inline corrected-drift reporting
- Made every major dock section collapsible
- Hid technical diagnostics behind a dedicated show/hide control

## Important limitations

Sync Guardian is experimental software intended for controlled testing before use on an important live production.

- Timestamp drift does not always equal viewer-visible lip-sync error exactly
- Adaptive Soft Sync still requires broad real-world testing across OBS, DistroAV, NDI, audio hardware, and network configurations
- Receiver resets may create a brief interruption while DistroAV reconnects
- Large sender-side timestamp discontinuities cannot always be repaired without a reset
- Hardware clock locking, combined A/V NDI streams, or professional synchronization systems may still be preferable in demanding environments

A local recording with visible and audible sync markers remains the best way to verify final viewer-facing alignment.

## License

See [LICENSE](LICENSE).
