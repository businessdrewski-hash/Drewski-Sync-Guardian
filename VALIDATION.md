# Validation notes

Performed on 2026-07-14:

- `plugin-main.cpp` passed Clang C++17 syntax checking with `-Wall -Wextra -Wpedantic -Werror` against locally defined OBS/Qt interface stubs.
- The CMake project configured and generated successfully with Ninja against mock imported targets named `OBS::libobs`, `OBS::obs-frontend-api`, `Qt6::Core`, and `Qt6::Widgets`.
- Static review covered:
  - Audio callback/GUI-thread separation through atomics
  - Reset ticket ownership and exact settings restoration
  - Prevention of overlapping reset pulses
  - Startup, cooldown, and post-reset suppression windows
  - Rolling-window pruning and 30-second baseline calibration
  - Single-jump non-action behavior
  - Target selection from A/V drift direction
  - Recovery verification and one-time escalation
  - Hourly automatic-reset limit enforcement
  - Plugin-unload finalization of active incident reports
- DistroAV's current `ndi-source.cpp` was checked to confirm that changing `ndi_framesync` is one of the changes that requires its internal NDI receiver to be reset.

Not performed in this environment:

- Linking against the real Windows OBS Studio 32.1.2 SDK and its matching Qt build.
- Loading the resulting DLL inside OBS.
- Live NDI reset, timestamp, or incident-report testing on the user's two-PC setup.
- Validation that every NDI source always exposes a CPU-readable asynchronous frame through `obs_source_get_frame` on the target system.

Treat v0.2.0 as a controlled-test source build, not a production-live-stream release.
