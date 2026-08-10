"""Mirrors specs/07-test-cases.md TC-DOM-023..037 (D3 eye closure)."""

import pytest

from drowsyguard.config import D3Config
from drowsyguard.domains.d3_eyeclosure import D3EyeClosure
from drowsyguard.domains.types import DomainState, FrameObservation


@pytest.fixture
def config() -> D3Config:
    return D3Config(
        active_dwell_ms=800,
        severe_dwell_ms=1500,
        critical_dwell_ms=3000,
        clear_dwell_ms=1000,
        eye_conf_min=0.50,
        perclos_window_ms=60000,
        perclos_p=0.80,
        perclos_min_samples=300,
        perclos_active=0.08,
        perclos_severe=0.15,
        perclos_hyst=0.02,
    )


def _obs(
    now_ms: float, eye_closed: bool | None, conf: float = 1.0, sunglasses: bool = False
) -> FrameObservation:
    return FrameObservation(
        timestamp_ms=now_ms,
        face_present=True,
        face_confidence=1.0,
        eye_closed=eye_closed,
        eye_confidence=conf,
        sunglasses_detected=sunglasses,
        mouth_open=False,
        mouth_confidence=1.0,
        yaw_deg=None,
        pitch_deg=None,
    )


def _closed_for(d3: D3EyeClosure, start_ms: float, duration_ms: float, step_ms: float = 50.0,
                 conf: float = 1.0) -> float:
    """Inclusive of the boundary sample -- see the comment on d1's _feed()."""
    t = start_ms
    while t <= start_ms + duration_ms:
        d3.update(_obs(t, True, conf=conf), t)
        t += step_ms
    return t


def _open_for(
    d3: D3EyeClosure, start_ms: float, duration_ms: float, step_ms: float = 50.0
) -> float:
    t = start_ms
    while t <= start_ms + duration_ms:
        d3.update(_obs(t, False), t)
        t += step_ms
    return t


def test_blink_400ms_stays_idle(config):
    """TC-DOM-023"""
    d3 = D3EyeClosure(config)
    _closed_for(d3, 0, 400)
    assert d3.state == DomainState.IDLE


def test_active_at_800ms(config):
    """TC-DOM-024"""
    d3 = D3EyeClosure(config)
    _closed_for(d3, 0, 800, step_ms=25)
    assert d3.state == DomainState.ACTIVE


def test_severe_at_1500ms(config):
    """TC-DOM-025"""
    d3 = D3EyeClosure(config)
    _closed_for(d3, 0, 1500, step_ms=25)
    assert d3.state == DomainState.SEVERE


def test_critical_at_3000ms(config):
    """TC-DOM-026"""
    d3 = D3EyeClosure(config)
    _closed_for(d3, 0, 3000, step_ms=25)
    assert d3.state == DomainState.CRITICAL


def test_500_normal_blinks_never_reach_active(config):
    """TC-DOM-027 (approximated): a real ~15 blinks/minute rate (a blink
    every 4 s, closed for 200 ms of it) run through a UNIFORM 100 ms sample
    step for 400 s of simulated time (100 blink cycles) -- NOT 500 blinks
    back-to-back, which would itself be an abnormal blink rate and isn't
    what "500 normal blinks" in the spec's C-BASE corpus means. The sample
    step must stay uniform across the closed/open phases: PERCLOS here is
    computed as closed-sample-fraction (see d3_eyeclosure.py module
    docstring), which only equals the real time-weighted fraction if
    samples are evenly spaced -- exactly the assumption a fixed camera
    frame rate satisfies in production."""
    d3 = D3EyeClosure(config)
    cycle_ms = 4000.0
    step_ms = 100.0
    t = 0.0
    while t <= 100 * cycle_ms:
        closed = (t % cycle_ms) < 200.0
        d3.update(_obs(t, closed), t)
        assert d3.state == DomainState.IDLE
        t += step_ms


def test_low_confidence_frame_holds_accumulator(config):
    """TC-DOM-028"""
    d3 = D3EyeClosure(config)
    t = _closed_for(d3, 0, 390)
    d3.update(_obs(t, True, conf=0.3), t)  # low confidence -- holds
    t += 50
    _closed_for(d3, t, 410)
    assert d3.state == DomainState.ACTIVE  # 390+410 = 800ms of valid evidence


def test_wall_clock_dwell_independent_of_frame_rate(config):
    """TC-DOM-029 / DOM-FLT-001: dwell is measured in elapsed wall-clock
    time, never frame count. At 10 FPS (100 ms step), the 8th sample lands
    exactly on t=800 and ACTIVE fires there. At 3 FPS (333 ms step), no
    sample lands exactly on 800 -- frames arrive at 0/333/666/999 -- so
    ACTIVE correctly cannot fire before t=999 (there is no earlier frame to
    fire it on), but it DOES fire at t=999, because 999 ms have genuinely
    elapsed, not because "N frames" elapsed. A frame-count-based (wrong)
    implementation would instead need the same *number* of frames (8) at
    3 FPS too, i.e. not until t=2331 -- that's the bug this test exists to
    catch if it's ever reintroduced."""
    d3_fast = D3EyeClosure(config)
    _closed_for(d3_fast, 0, 800, step_ms=100)
    assert d3_fast.state == DomainState.ACTIVE

    d3_slow = D3EyeClosure(config)
    d3_slow.update(_obs(0, True), 0)
    d3_slow.update(_obs(333, True), 333)
    d3_slow.update(_obs(666, True), 666)
    assert d3_slow.state == DomainState.IDLE  # only 666 ms elapsed so far
    d3_slow.update(_obs(999, True), 999)
    assert d3_slow.state == DomainState.ACTIVE  # 999 ms elapsed, on the 4th frame


def test_perclos_invalid_before_min_samples(config):
    """TC-DOM-030"""
    d3 = D3EyeClosure(config)
    _open_for(d3, 0, 20_000, step_ms=100)  # 200 samples, below 300
    assert d3.perclos is None


def test_perclos_009_reaches_active(config):
    """TC-DOM-031"""
    d3 = D3EyeClosure(config)
    t = 0.0
    for i in range(400):
        closed = (i % 100) < 9  # ~9% closed
        d3.update(_obs(t, closed), t)
        t += 100
    assert d3.perclos is not None
    assert d3.perclos >= config.perclos_active
    assert d3.state >= DomainState.ACTIVE


def test_perclos_016_reaches_severe(config):
    """TC-DOM-032"""
    d3 = D3EyeClosure(config)
    t = 0.0
    for i in range(400):
        closed = (i % 100) < 16
        d3.update(_obs(t, closed), t)
        t += 100
    assert d3.perclos >= config.perclos_severe
    assert d3.state == DomainState.SEVERE


def test_perclos_hysteresis_blocks_small_dip(config):
    """TC-DOM-033: reach SEVERE, then feed a stream at ~14.5% (within
    hyst of 15%) and confirm it does not drop below SEVERE."""
    d3 = D3EyeClosure(config)
    t = 0.0
    for i in range(400):
        d3.update(_obs(t, (i % 100) < 20), t)
        t += 100
    assert d3.state == DomainState.SEVERE

    for i in range(400):
        d3.update(_obs(t, (i % 1000) < 145), t)
        t += 100
    assert d3.state == DomainState.SEVERE


def test_perclos_alone_never_reaches_critical(config):
    """TC-DOM-034"""
    d3 = D3EyeClosure(config)
    t = 0.0
    for i in range(400):
        d3.update(_obs(t, (i % 10) < 6), t)  # 60% closed, but no long single closure
        t += 100
    assert d3.state == DomainState.SEVERE
    assert d3.state != DomainState.CRITICAL


def test_unavailable_when_confidence_low_30pct_of_3s(config):
    """TC-DOM-036 (sunglasses proxy): >30% low-confidence frames in the
    last 3 s marks UNAVAILABLE and D3 never reports IDLE."""
    d3 = D3EyeClosure(config)
    t = 0.0
    for i in range(40):
        low_conf = (i % 2) == 0  # 50% low confidence
        d3.update(_obs(t, False, conf=0.1 if low_conf else 1.0), t)
        t += 75
    assert d3.unavailable is True
    assert d3.state != DomainState.IDLE


def test_sunglasses_forces_unavailable(config):
    d3 = D3EyeClosure(config)
    d3.update(_obs(0, False, sunglasses=True), 0)
    assert d3.unavailable is True
    assert d3.state != DomainState.IDLE


def test_critical_requires_full_clear_dwell_to_leave(config):
    """DOM-D3-007: CRITICAL only clears after clear_dwell_ms of confirmed
    open eyes, not on the very next open frame."""
    d3 = D3EyeClosure(config)
    t = _closed_for(d3, 0, 3000, step_ms=25)
    assert d3.state == DomainState.CRITICAL

    t2 = _open_for(d3, t, 500, step_ms=25)  # < clear_dwell_ms
    assert d3.state == DomainState.CRITICAL

    _open_for(d3, t2, 600, step_ms=25)  # now >= 1000ms total open
    assert d3.state != DomainState.CRITICAL
