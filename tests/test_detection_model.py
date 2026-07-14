"""Small dependency-free sanity model for Sync Guardian v0.2 defaults."""

VIDEO_STALL_MS = 1000
AUDIO_STALL_MS = 1000
DRIFT_THRESHOLD_MS = 200
DRIFT_PERSISTENCE_MS = 10_000


def choose_issue(video_age, desktop_age, mic_age, drift, drift_duration_ms, mic_expected=True):
    video_fresh = video_age < VIDEO_STALL_MS
    desktop_fresh = desktop_age < AUDIO_STALL_MS
    if video_age >= VIDEO_STALL_MS and desktop_fresh:
        return "video"
    if desktop_age >= AUDIO_STALL_MS and video_fresh:
        return "desktop"
    if mic_expected and mic_age >= AUDIO_STALL_MS and (video_fresh or desktop_fresh):
        return "mic"
    if video_fresh and desktop_fresh and abs(drift) >= DRIFT_THRESHOLD_MS and drift_duration_ms >= DRIFT_PERSISTENCE_MS:
        return "video" if drift < 0 else "desktop"
    return None


def run():
    assert choose_issue(20, 20, 20, 70, 60_000) is None, "A 70 ms jump must not reset at default thresholds"
    assert choose_issue(1200, 20, 20, 0, 0) == "video"
    assert choose_issue(20, 1200, 20, 0, 0) == "desktop"
    assert choose_issue(20, 20, 1200, 0, 0) == "mic"
    assert choose_issue(20, 20, 20, -250, 9_000) is None
    assert choose_issue(20, 20, 20, -250, 10_000) == "video"
    assert choose_issue(20, 20, 20, 250, 10_000) == "desktop"
    print("Sync Guardian detection model sanity checks passed")


if __name__ == "__main__":
    run()
