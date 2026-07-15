# Validation notes — v0.3.3

Validated in the dependency-free model and source review:

- Internal measurements still run every 250 ms and visible diagnostics refresh every 1000 ms.
- Persistent drift detection uses corrected output drift only while Adaptive Soft Sync and its desktop filter are active.
- With Adaptive Soft Sync disabled, threshold behavior returns to raw transport drift.
- A raw `-250 ms` drift corrected to approximately `0 ms` does not trigger the ordinary threshold in Soft Sync mode.
- The same raw value triggers normally when Soft Sync is disabled.
- Corrected drift beyond the configured threshold still triggers recovery.
- Full-group and targeted reset restoration continue to use non-destructive settings updates and restore OBS-level source state.
- Major UI sections are collapsible and technical diagnostics default to hidden.
- GitHub Actions and installer metadata are v0.3.3.

The Windows DLL and installer must still be compiled by the included GitHub Actions workflow for full OBS runtime validation.
