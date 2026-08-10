"""Mirrors specs/07-test-cases.md TC-DOM-011..021 (D2 yawning)."""

import pytest

from drowsyguard.config import D2Config
from drowsyguard.domains.d2_yawn import D2Yawn
from drowsyguard.domains.types import DomainState, FrameObservation


@pytest.fixture
def config() -> D2Config:
    return D2Config(
        mar_open=0.60,
        yawn_min_ms=1500,
        yawn_max_ms=12000,
        window_ms=120000,
        active_count=2,
        severe_count=3,
        severe_single_ms=5000,
    )


def _obs(now_ms: float, mouth_open: bool | None) -> FrameObservation:
    return FrameObservation(
        timestamp_ms=now_ms,
        face_present=True,
        face_confidence=1.0,
        eye_closed=False,
        eye_confidence=1.0,
        sunglasses_detected=False,
        mouth_open=mouth_open,
        mouth_confidence=1.0,
        yaw_deg=None,
        pitch_deg=None,
    )


def _mouth_open_episode(d2: D2Yawn, start_ms: float, duration_ms: float, step_ms: float = 100.0):
    t = start_ms
    while t < start_ms + duration_ms:
        d2.update(_obs(t, True), t)
        t += step_ms
    d2.update(_obs(t, False), t)
    return t


def test_episode_below_min_does_not_register(config):
    """TC-DOM-011"""
    d2 = D2Yawn(config)
    _mouth_open_episode(d2, 0, 1400)
    assert d2.event_count == 0


def test_episode_at_1600ms_registers_one_event(config):
    """TC-DOM-012"""
    d2 = D2Yawn(config)
    _mouth_open_episode(d2, 0, 1600)
    assert d2.event_count == 1


def test_continuous_speech_never_registers(config):
    """TC-DOM-013: bursts of 200 ms open / 200 ms closed for 3 minutes."""
    d2 = D2Yawn(config)
    t = 0.0
    while t < 180_000:
        t = _mouth_open_episode(d2, t, 200, step_ms=50)
        t += 200
    assert d2.event_count == 0


def test_overlong_episode_discarded_and_flagged_degraded(config):
    """TC-DOM-015"""
    d2 = D2Yawn(config)
    t = 0.0
    while t < 13_000:
        d2.update(_obs(t, True), t)
        t += 200
    assert d2.model_degraded is True
    d2.update(_obs(t, False), t)
    assert d2.event_count == 0


def test_single_yawn_never_alarms(config):
    """TC-DOM-016 -- the most important D2 test case."""
    d2 = D2Yawn(config)
    _mouth_open_episode(d2, 0, 2000)
    assert d2.state == DomainState.IDLE


def test_two_yawns_within_window_go_active(config):
    """TC-DOM-017"""
    d2 = D2Yawn(config)
    t = _mouth_open_episode(d2, 0, 2000)
    t = _mouth_open_episode(d2, t + 100_000, 2000)
    assert d2.state == DomainState.ACTIVE


def test_two_yawns_outside_window_stay_idle(config):
    """TC-DOM-018"""
    d2 = D2Yawn(config)
    t = _mouth_open_episode(d2, 0, 2000)
    t = _mouth_open_episode(d2, t + 130_000, 2000)
    assert d2.state == DomainState.IDLE


def test_three_yawns_go_severe(config):
    """TC-DOM-019"""
    d2 = D2Yawn(config)
    t = 0.0
    for _ in range(3):
        t = _mouth_open_episode(d2, t, 2000)
        t += 10_000
    assert d2.state == DomainState.SEVERE


def test_two_yawns_one_long_goes_severe(config):
    """TC-DOM-020"""
    d2 = D2Yawn(config)
    t = _mouth_open_episode(d2, 0, 2000)
    _mouth_open_episode(d2, t + 10_000, 5500)
    assert d2.state == DomainState.SEVERE


def test_acknowledge_does_not_reset_window(config):
    """TC-DOM-021"""
    d2 = D2Yawn(config)
    t = _mouth_open_episode(d2, 0, 2000)
    _mouth_open_episode(d2, t + 10_000, 2000)
    assert d2.state == DomainState.ACTIVE
    d2.acknowledge()
    assert d2.event_count == 2
    assert d2.state == DomainState.ACTIVE
