# Sync Guardian

Sync Guardian is an experimental OBS Studio dock for monitoring DistroAV/NDI receiver sources, detecting possible synchronization problems, and providing targeted recovery controls.

Install it on the **receiving/streaming PC** where the NDI sources are added to OBS.

## Features

- Monitors one NDI video source, one desktop-audio source, and one microphone source
- Estimates video-to-audio timestamp drift from a calibrated baseline
- Detects stalled video or audio sources
- Shows a short current and session-history health summary
- Highlights the most likely manual recovery button when an issue is detected
- Provides targeted resets for video, individual audio sources, both audio sources, or the entire NDI group
- Supports **Observe only**, **Ask before resetting**, and **Fully automatic** modes
- Includes cooldowns, reset limits, grace periods, and recovery verification
- Uses a compact, scrollable dock suitable for 1080p displays

## Install

Download and extract the compiled Windows artifact.

The extracted package should contain:

```text
obs-plugins\
  64bit\
    sync-guardian.dll

data\
  obs-plugins\
    sync-guardian\
      locale\
        en-US.ini
```

1. Close OBS completely.
2. Select the extracted `obs-plugins` and `data` folders.
3. Drag both folders into:

```text
C:\Program Files\obs-studio\
```

4. Allow Windows to merge the folders and replace files when updating.
5. Start OBS.
6. Open **Docks → Sync Guardian**.

The DLL should end up at:

```text
C:\Program Files\obs-studio\obs-plugins\64bit\sync-guardian.dll
```

Do not place the plugin DLL in `bin\64bit`.

## Initial setup

1. Open **Docks → Sync Guardian**.
2. Click **Refresh Source List**.
3. Map the NDI video, desktop-audio, and microphone receiver sources.
4. Leave the mode on **Observe only** during initial testing.
5. Allow the sources to run long enough for Sync Guardian to establish a stable baseline.
6. Test each manual recovery action during a non-critical recording before enabling automatic recovery.

## Operating modes

- **Observe only** — monitors and reports issues without resetting anything.
- **Ask before resetting** — recommends a recovery action and asks for confirmation.
- **Fully automatic** — performs a targeted recovery after an issue remains beyond the configured thresholds.

A single timestamp jump does not trigger an automatic reset by itself.

## Updating

1. Download and extract the new compiled artifact.
2. Close OBS completely.
3. Drag the new `obs-plugins` and `data` folders into:

```text
C:\Program Files\obs-studio\
```

4. Approve folder merging and file replacement.
5. Restart OBS.

Existing Sync Guardian mappings and settings should remain in the OBS plugin configuration directory.

## Troubleshooting

### Sync Guardian is not listed under Docks

Confirm the DLL is located at:

```text
C:\Program Files\obs-studio\obs-plugins\64bit\sync-guardian.dll
```

Then open **Help → Log Files → View Current Log** and search for `sync-guardian`.

### NDI sources do not appear in the mapping lists

- Confirm the plugin is installed on the receiving/streaming PC.
- Confirm the DistroAV receiver sources already exist in OBS.
- Click **Refresh Source List**.

### The interface does not fit

The dock is vertically scrollable. Make sure **Docks → Lock Docks** is disabled if you need to resize or reposition it.

## Status

Sync Guardian is experimental. Use **Observe only** first and validate detection and manual recovery behavior before relying on automatic recovery during an important stream.
