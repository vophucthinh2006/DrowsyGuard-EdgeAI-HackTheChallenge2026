"""Mirrors specs/07-test-cases.md TC-DOM-001..009 (D1 distraction)."""

import pytest

from drowsyguard.config import D1Config
from drowsyguard.domains.d1_distraction import D1Distraction
from drowsyguard.domains.types import DomainState, FrameObservation


@pytest.fixture
def config() -> D1Config:
    return D1Config(
        yaw_limit_deg=30,
        pitch_down_limit_deg=20,
        glance_min_ms=600,
        active_dwell_ms=2000,
        severe_cum_ms=6000,
        cum_window_ms=12000,
        clear_dwell_ms=3000,
        indicator_suppress_ms=3000,
    )


def _obs(now_ms: float, yaw: float = 0.0, pitch: float = 0.0, **kw) -> FrameObservation:
    defaults = dict(
        timestamp_ms=now_ms,
        face_present=True,
        face_confidence=1.0,
        eye_closed=False,
        eye_confidence=1.0,
        sunglasses_detected=False,
        mouth_open=False,
        mouth_confidence=1.0,
        yaw_deg=yaw,
        pitch_deg=pitch,
    )
    defaults.update(kw)
    return FrameObservation(**defaults)


def _feed(d1: D1Distraction, yaw: float, pitch: float, start_ms: float, duration_ms: float,
          step_ms: float = 100.0) -> float:
    """Inclusive of the boundary sample at start_ms + duration_ms, so a
    dwell threshold set to exactly `duration_ms` is actually reached -- the
    accumulator's elapsed time is measured relative to the first sample's
    timestamp, so the LAST call's timestamp is what has to hit the
    boundary, not "one step before it"."""
    t = start_ms
    while t <= start_ms + duration_ms:
        d1.update(_obs(t, yaw=yaw, pitch=pitch), t)
        t += step_ms
    return t


def test_glance_below_noise_floor_does_not_count(config):
    """TC-DOM-001"""
    d1 = D1Distraction(config)
    t = _feed(d1, yaw=45, pitch=0, start_ms=0, duration_ms=500)
    d1.update(_obs(t, yaw=0, pitch=0), t)
    assert d1.state == DomainState.IDLE


def test_active_at_2000ms_continuous(config):
    """TC-DOM-002"""
    d1 = D1Distraction(config)
    _feed(d1, yaw=45, pitch=0, start_ms=0, duration_ms=2000, step_ms=50)
    assert d1.state == DomainState.ACTIVE


def test_not_active_at_1900ms(config):
    """TC-DOM-003"""
    d1 = D1Distraction(config)
    t = _feed(d1, yaw=45, pitch=0, start_ms=0, duration_ms=1900, step_ms=50)
    d1.update(_obs(t, yaw=0, pitch=0), t)
    assert d1.state == DomainState.IDLE


def test_severe_via_cumulative_6s_in_12s_window(config):
    """TC-DOM-004: six 1.1 s glances inside a 12 s window."""
    d1 = D1Distraction(config)
    t = 0.0
    for _ in range(6):
        t = _feed(d1, yaw=45, pitch=0, start_ms=t, duration_ms=1100, step_ms=50)
        t = _feed(d1, yaw=0, pitch=0, start_ms=t, duration_ms=50, step_ms=50)
    assert d1.state == DomainState.SEVERE


def test_severe_via_4s_continuous(config):
    """TC-DOM-005"""
    d1 = D1Distraction(config)
    _feed(d1, yaw=45, pitch=0, start_ms=0, duration_ms=4000, step_ms=50)
    assert d1.state == DomainState.SEVERE


def test_does_not_deescalate_before_clear_dwell(config):
    """TC-DOM-006"""
    d1 = D1Distraction(config)
    t = _feed(d1, yaw=45, pitch=0, start_ms=0, duration_ms=2000, step_ms=50)
    assert d1.state == DomainState.ACTIVE
    t = _feed(d1, yaw=0, pitch=0, start_ms=t, duration_ms=2900, step_ms=50)
    assert d1.state == DomainState.ACTIVE
    _feed(d1, yaw=45, pitch=0, start_ms=t, duration_ms=100, step_ms=50)
    assert d1.state != DomainState.IDLE


def test_indicator_suppresses_yaw_in_indicated_direction(config):
    """TC-DOM-007: indicator right suppresses a rightward (positive) yaw."""
    d1 = D1Distraction(config)
    t = 0.0
    d1.update(_obs(t, yaw=0, pitch=0, indicator_active=True, indicator_dir=2), t)
    _feed(d1, yaw=45, pitch=0, start_ms=t, duration_ms=3000, step_ms=50)
    assert d1.state == DomainState.IDLE


def test_indicator_does_not_suppress_pitch(config):
    """TC-DOM-008"""
    d1 = D1Distraction(config)
    t = 0.0
    d1.update(_obs(t, yaw=0, pitch=0, indicator_active=True, indicator_dir=2), t)
    _feed(d1, yaw=0, pitch=30, start_ms=t, duration_ms=3000, step_ms=50)
    assert d1.state == DomainState.ACTIVE


def test_indicator_does_not_suppress_opposite_direction(config):
    """TC-DOM-009"""
    d1 = D1Distraction(config)
    t = 0.0
    d1.update(_obs(t, yaw=0, pitch=0, indicator_active=True, indicator_dir=2), t)
    _feed(d1, yaw=-45, pitch=0, start_ms=t, duration_ms=3000, step_ms=50)
    assert d1.state == DomainState.ACTIVE


def test_missing_pose_holds_rather_than_alarms(config):
    """No spec-07 ID (D1 has no None-input case listed) -- added because
    the live inference backend currently always produces yaw_deg=None
    (see inference/blazeface_cnn_backend.py); this is the behaviour that
    fact depends on."""
    d1 = D1Distraction(config)
    for i in range(50):
        d1.update(_obs(i * 100, yaw=None, pitch=None), i * 100)
    assert d1.state == DomainState.IDLE
