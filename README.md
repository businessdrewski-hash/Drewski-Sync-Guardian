# Sync Guardian v0.2.0 — one-click GitHub build

This repository is prepared to compile Sync Guardian for **OBS Studio 32.1.2, Windows x64** using GitHub Actions. Nothing needs to be installed locally.

## Build it

1. Sign in to GitHub and create a new **empty** repository named `obs-sync-guardian`.
2. Extract this ZIP on your PC.
3. In the empty repository, choose **Add file → Upload files**.
4. Drag every extracted file and folder into the upload page, including the `.github` folder, then commit directly to `main`.
5. Open **Actions → Build Sync Guardian for Windows → Run workflow → Run workflow**.
6. Open the completed run and download the artifact named `sync-guardian-0.2.0-windows-x64`.

The first upload may automatically start a build because the workflow also runs on pushes to `main`.

## Install it

1. Close OBS completely.
2. Extract the downloaded GitHub artifact, then extract the Windows plugin ZIP inside it if GitHub wrapped it in another ZIP.
3. Copy the plugin package folders into your OBS installation so they merge with:
   - `C:\Program Files\obs-studio\obs-plugins\64bit\`
   - `C:\Program Files\obs-studio\data\obs-plugins\sync-guardian\`
4. Start OBS.
5. Open **Docks → Sync Guardian**.
6. Leave the mode on **Observe only** for initial testing.

## Verify loading

Open **Help → Log Files → View Current Log** and search for:

```text
[sync-guardian] Loading version 0.2.0
```

## What the workflow does

The Windows runner downloads the official OBS plugin-template build infrastructure, downloads the OBS 32.1.2 sources and matching prebuilt dependencies, builds the plugin with Visual Studio 2022, packages it, and exposes the result as a downloadable workflow artifact.

The produced DLL is unsigned and experimental. Windows may show a warning when handling an unsigned binary. Test it during a non-critical recording before using automatic recovery during a live stream.
