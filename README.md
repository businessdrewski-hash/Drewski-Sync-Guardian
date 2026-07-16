# Sync Guardian for OBS Studio

Hi — I’m a YouTuber, not a professional software developer. I created this project to solve A/V sync problems in my own two-PC OBS/NDI setup, with substantial assistance from AI during coding, debugging, and documentation.
I’m making it public because the approach or code may help other people dealing with similar issues. This is an experimental, unofficial project provided as-is, without any guarantee that it will work with every system or remain compatible with future OBS, DistroAV, or NDI updates.
I may not have the time or technical ability to provide updates, individual support, or troubleshooting. Please feel free to study, modify, improve, fork, and redistribute the code, subject to the GPL and any applicable third-party licenses. Use it at your own risk, and contributions or better implementations are welcome.

**Sync Guardian** is an experimental OBS Studio plugin for people troubleshooting persistent A/V-sync problems in two-PC NDI® / DistroAV workflows.

It runs on the **receiving or streaming PC**, watches the DistroAV sources already present in OBS, measures gradual drift and timestamp failures, and can recover an individual receiver or rebuild a mapped video/audio group without intentionally wiping the source settings you already configured.

> Sync Guardian is a monitoring and recovery tool. It does not replace OBS Studio, DistroAV, NDI, proper network design, or hardware synchronization.

## Why it exists

In some two-PC workflows, video, desktop audio, and microphone audio arrive through separate NDI sources. Those sources may not always reconnect, buffer, or recover together. Over a long recording or stream, this can appear as:

- slow A/V drift;
- a sudden sync jump after a stall or reconnect;
- frozen video while audio continues;
- audio-only sources moving out of alignment with video;
- a DistroAV receiver that does not recover until its properties are touched;
- manual resets that accidentally change source settings.

Sync Guardian was created to make those failures visible and provide controlled recovery without requiring a full OBS restart. For new setups, carrying related video and audio through one combined sender or using hardware clocking may still be the simpler long-term solution.

## Main capabilities

### Live drift monitoring

Sync Guardian compares the mapped video and desktop-audio timelines, establishes a session baseline, and reports:

- drift from baseline;
- whether video is rushing or dragging;
- estimated drift rate in milliseconds per minute;
- raw transport drift;
- estimated corrected drift when Adaptive Soft Sync is enabled.

The visible display updates once per second while internal sampling runs more frequently. Median filtering and longer trend windows are used to reduce noisy short-term readings.

### Stall and timestamp-jump detection

The background watchdog can detect conditions such as:

- video frames no longer arriving;
- timestamps no longer advancing;
- large timestamp discontinuities;
- persistent drift outside the configured threshold;
- failed or incomplete recovery attempts.

Monitoring and recovery continue independently of dock painting, mouse movement, and whether OBS is focused.

### Targeted receiver reset

A single mapped DistroAV source can be reset without restarting OBS. Sync Guardian briefly changes a receiver property to force DistroAV to recreate the receiver, then restores the captured configuration, including the original FrameSync state.

### Full sync-group rebuild

When one-source recovery is not enough, Sync Guardian can rebuild the mapped video, desktop-audio, and microphone sources together. The restoration path preserves relevant DistroAV and OBS settings, including:

- selected NDI source and receiver properties;
- FrameSync, timing, and latency settings;
- OBS sync offset and audio-track routing;
- monitoring mode, volume, mute, and balance;
- enabled state, source flags, and deinterlacing settings.

### Recovery modes and safety controls

Three operating modes are available:

- **Observe only** — monitor and log without changing sources;
- **Ask before recovery** — request confirmation before a reset;
- **Fully automatic** — perform configured recovery after a problem persists.

Safety controls include startup and scene-change grace periods, cooldowns, verification, escalation limits, and a maximum number of automatic resets per hour.

### Adaptive Soft Sync

Adaptive Soft Sync is an optional experimental audio-resampling system for correcting slow, steady drift without immediately using a large audible cut.

It applies a small rate adjustment to the mapped desktop-audio source, measured in parts per million (ppm), and can optionally apply linked correction to the microphone source. Correction changes gradually and can be disabled and removed at any time.

The default correction limit is intentionally conservative. Higher limits may make catch-up faster but can become audible on pitch-sensitive material. Treat high-range correction as a temporary recovery ceiling, not a normal operating target.

### Diagnostics and event history

Sync Guardian records important events such as stalls, timestamp jumps, persistent-drift episodes, reset attempts, verification results, escalation, and cooldown states. Technical details can remain collapsed during normal use and expanded when troubleshooting.

## Recommended first-time setup

1. Install Sync Guardian on the **receiving/streaming PC**.
2. Open **OBS → Docks → Sync Guardian**.
3. Map the DistroAV video, desktop-audio, and optional microphone sources.
4. Begin in **Observe only** mode.
5. Let the system establish a baseline and run long enough to identify the normal drift pattern.
6. Test manual targeted resets and confirm all source settings are restored correctly.
7. Configure conservative thresholds before enabling automatic recovery.
8. Leave Adaptive Soft Sync disabled until ordinary monitoring and reset behavior are confirmed stable.

For split-source setups, test audio FrameSync carefully. Some systems behave better with FrameSync disabled on audio-only NDI sources.

## Installation

### Windows installer

Download the current Windows artifact from the repository's [Releases](https://github.com/businessdrewski-hash/Sync-Guardian-For-NDI-and-OBS-Studio/releases) or [Actions](https://github.com/businessdrewski-hash/Sync-Guardian-For-NDI-and-OBS-Studio/actions) page, close OBS, and run:

```text
SyncGuardian-Setup.exe
```

The installer adds normal Windows uninstall support.

### Portable installation

Extract `SyncGuardian-Portable.zip`, then copy its `obs-plugins` and `data` folders into the OBS installation directory, normally:

```text
C:\Program Files\obs-studio\
```

The plugin DLL should end up at:

```text
C:\Program Files\obs-studio\obs-plugins\64bit\sync-guardian.dll
```

Restart OBS and open **Docks → Sync Guardian**.

## Building from source

Run the **Build Sync Guardian for Windows** workflow from the repository's Actions tab. The generated artifact contains:

- `SyncGuardian-Setup.exe`;
- `SyncGuardian-Portable.zip`;
- `SHA256SUMS.txt`.

The source ZIP downloaded from GitHub does not itself contain a compiled plugin DLL.

See [CHANGELOG.md](CHANGELOG.md) and [RELEASE-NOTES.md](RELEASE-NOTES.md) for version-specific changes.

## Important limitations

Sync Guardian is experimental software. Test it thoroughly before relying on it during an important production.

- Measured timestamp drift does not always equal viewer-visible lip-sync error exactly.
- Receiver resets can cause a brief interruption while DistroAV reconnects.
- Adaptive Soft Sync cannot repair every discontinuity and may become audible at aggressive settings.
- Large sender-side failures may still require a group rebuild or manual intervention.
- Network congestion, capture-hook failures, OBS rendering problems, driver issues, and unsynchronized hardware clocks are outside the plugin's direct control.
- A local recording with visible and audible sync markers remains the best final verification method.

## Credits

Sync Guardian is built for use with the work of these open-source and third-party projects:

- **OBS Studio** — the free and open-source recording and streaming application and plugin API maintained by the OBS Project and its contributors: [website](https://obsproject.com/) · [source](https://github.com/obsproject/obs-studio) · [developer documentation](https://obsproject.com/docs/) · [license](https://github.com/obsproject/obs-studio/blob/master/COPYING)
- **DistroAV** — the OBS integration that provides NDI video/audio sources, outputs, and receiver controls used by this plugin: [website](https://distroav.org/) · [source](https://github.com/DistroAV/DistroAV) · [license](https://github.com/DistroAV/DistroAV/blob/master/LICENSE)
- **NDI technology** — the network audio/video technology used by DistroAV: [NDI website](https://ndi.video/) · [documentation](https://docs.ndi.video/) · [SDK licensing information](https://docs.ndi.video/all/developing-with-ndi/sdk/licensing)

Sync Guardian is an independent third-party project. It is not an official OBS Project, DistroAV, or Vizrt NDI AB product and is not endorsed by those organizations.

## License and trademarks

Sync Guardian is free software distributed under the **GNU General Public License version 2** as provided in [LICENSE](LICENSE). If you distribute compiled builds or modified versions, keep the license and notices intact and provide the corresponding source code as required by the GPL.

OBS and OBS Studio are trademarks of Wizards of OBS LLC. **NDI® is a registered trademark of Vizrt NDI AB.** Other names and marks belong to their respective owners.

This software is provided **as is**, without warranty of any kind. Use it at your own risk.
