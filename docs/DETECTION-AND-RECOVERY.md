# Detection and recovery design

## Offset estimate

For each stream, Sync Guardian stores the most recent source timestamp and the monotonic wall-clock time at which that timestamp was observed.

The current stream position is estimated as:

```text
projected position = last source timestamp + (now - callback observation time)
```

The raw A/V estimate is:

```text
projected video position - projected desktop-audio position
```

A five-second rolling median is used as the operational value.

## Baseline calibration

After the startup grace period, the plugin waits until it has approximately 30 seconds of fresh A/V samples. Calibration is accepted only when the sample range stays within the larger of:

- 25 ms
- 25% of the configured persistent-drift threshold

A timestamp discontinuity can therefore delay calibration until the unstable sample leaves the 30-second window.

## Conditions

### Video stall

- Video age exceeds the configured video threshold.
- Desktop audio remains fresh.
- Condition remains true for at least 500 ms after crossing the packet-age threshold.

### Desktop-audio stall

- Desktop-audio age exceeds the configured audio threshold.
- Video remains fresh.
- Condition remains true for at least 500 ms.

### Microphone stall

- The mapped microphone source is enabled and active.
- Microphone age exceeds the configured audio threshold.
- Video or desktop audio remains fresh.
- Condition remains true for at least 500 ms.

### Persistent A/V drift

- A valid calibrated baseline exists.
- Video and desktop audio remain fresh.
- Absolute filtered deviation exceeds the configured threshold.
- The condition remains true for the configured persistence duration.

## Confidence

Base confidence:

- Video stall: 95
- Desktop-audio stall: 95
- Microphone stall: 90
- Persistent drift: 75

Additional evidence:

- Timestamp discontinuity within five seconds: +15
- Drift rate of at least 30 ms/min moving farther from baseline: +10

Confidence is capped at 100.

A timestamp discontinuity alone has no recovery target and cannot trigger a reset.

## Recovery verification

Packet-stall incidents verify when the affected source becomes fresh.

Persistent-drift incidents verify when:

- Both video and desktop audio are fresh; and
- The deviation is below 75% of the configured threshold, or it improved by at least 50% from its pre-reset value.

If verification fails, one full-group rebuild may be attempted, subject to the hourly reset limit. A second failed verification ends automatic action and starts the configured cooldown.
