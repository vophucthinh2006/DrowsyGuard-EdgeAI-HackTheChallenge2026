"""Mirrors specs/07-test-cases.md TC-FUS-001..018 (fusion / alert ladder)."""

import pytest

from drowsyguard.config import FusionConfig
from drowsyguard.domains.types import DomainState
from drowsyguard.fusion.ladder import AlertLadder, AlertLevel, FusionInputs

IDLE, ACTIVE, SEVERE, CRITICAL = (
    DomainState.IDLE,
    DomainState.ACTIVE,
    DomainState.SEVERE,
    DomainState.CRITICAL,
)


@pytest.fixture
def config() -> FusionConfig:
    return FusionConfig(
        l2_escalate_ms=10000,
        level_clear_ms=5000,
        ack_refractory_ms=60000,
        ack_max_consecutive=3,
    )


def _inputs(d1=IDLE, d2=IDLE, d3=IDLE, sensor_lost=False, d3_unavailable=False) -> FusionInputs:
    return FusionInputs(d1=d1, d2=d2, d3=d3, sensor_lost=sensor_lost, d3_unavailable=d3_unavailable)


def test_single_active_domain_is_l1(config):
    """TC-FUS-001/002/003"""
    for kwargs in ({"d1": ACTIVE}, {"d2": ACTIVE}, {"d3": ACTIVE}):
        ladder = AlertLadder(config)
        assert ladder.update(_inputs(**kwargs), 0) == AlertLevel.L1_EARLY


def test_two_active_domains_is_l2(config):
    """TC-FUS-004"""
    ladder = AlertLadder(config)
    assert ladder.update(_inputs(d1=ACTIVE, d2=ACTIVE), 0) == AlertLevel.L2_DROWSY


def test_one_severe_domain_is_l2(config):
    """TC-FUS-005"""
    ladder = AlertLadder(config)
    assert ladder.update(_inputs(d2=SEVERE), 0) == AlertLevel.L2_DROWSY


def test_d1_severe_alone_never_reaches_l3_by_level_alone(config):
    """TC-FUS-006: only the L2-sustained-10s-no-ack path can bring a
    D1-only SEVERE to L3, never instantaneously (DOM-000)."""
    ladder = AlertLadder(config)
    level = ladder.update(_inputs(d1=SEVERE), 0)
    assert level == AlertLevel.L2_DROWSY
    assert level != AlertLevel.L3_DANGER


def test_d3_critical_goes_directly_to_l3(config):
    """TC-FUS-008"""
    ladder = AlertLadder(config)
    assert ladder.update(_inputs(d3=CRITICAL), 0) == AlertLevel.L3_DANGER


def test_l2_escalates_to_l3_after_10s_no_ack(config):
    """TC-FUS-009"""
    ladder = AlertLadder(config)
    ladder.update(_inputs(d2=SEVERE), 0)
    level = ladder.update(_inputs(d2=SEVERE), 9999)
    assert level == AlertLevel.L2_DROWSY
    level = ladder.update(_inputs(d2=SEVERE), 10000)
    assert level == AlertLevel.L3_DANGER


def test_ack_before_10s_prevents_escalation(config):
    """TC-FUS-010"""
    ladder = AlertLadder(config)
    ladder.update(_inputs(d2=SEVERE), 0)
    ladder.update(_inputs(d2=SEVERE), 9000)
    ladder.acknowledge(9000)
    level = ladder.update(_inputs(d2=IDLE), 9000)
    assert level == AlertLevel.L0_NORMAL


def test_ack_suppresses_l1_reentry_but_not_l2(config):
    """TC-FUS-011/012"""
    ladder = AlertLadder(config)
    ladder.update(_inputs(d2=SEVERE), 0)
    ladder.acknowledge(0)
    assert ladder.level == AlertLevel.L0_NORMAL

    level = ladder.update(_inputs(d1=ACTIVE), 5000)
    assert level == AlertLevel.L0_NORMAL  # L1 suppressed by refractory

    level = ladder.update(_inputs(d3=SEVERE), 5000)
    assert level == AlertLevel.L2_DROWSY  # L2 never suppressed


def test_ack_has_no_effect_at_l3(config):
    """TC-FUS-013"""
    ladder = AlertLadder(config)
    ladder.update(_inputs(d3=CRITICAL), 0)
    assert ladder.level == AlertLevel.L3_DANGER
    ladder.acknowledge(100)
    assert ladder.level == AlertLevel.L3_DANGER


def test_ack_saturation_after_four_within_10_minutes(config):
    """TC-FUS-014"""
    ladder = AlertLadder(config)
    for i in range(4):
        t = i * 60_000.0
        ladder.update(_inputs(d1=ACTIVE), t)
        ladder.acknowledge(t)
    assert ladder.ack_saturated is True


def test_does_not_deescalate_before_5s_idle(config):
    """TC-FUS-015. The idle dwell only starts counting from the first frame
    that actually reports IDLE (100 ms here) -- a single call at t=4900 with
    nothing in between does not mean 4900 ms of *observed* continuous idle,
    it means idle was observed once, at t=4900."""
    ladder = AlertLadder(config)
    ladder.update(_inputs(d2=SEVERE), 0)
    ladder.update(_inputs(), 100)  # idle dwell begins here
    level = ladder.update(_inputs(), 100 + 4900)
    assert level == AlertLevel.L2_DROWSY


def test_deescalates_one_level_at_5s_idle(config):
    """TC-FUS-016"""
    ladder = AlertLadder(config)
    ladder.update(_inputs(d2=SEVERE), 0)
    ladder.update(_inputs(), 100)  # idle dwell begins here
    level = ladder.update(_inputs(), 100 + 5000)
    assert level == AlertLevel.L1_EARLY  # one step, not straight to L0


def test_l3_never_auto_deescalates(config):
    """TC-FUS-017"""
    ladder = AlertLadder(config)
    ladder.update(_inputs(d3=CRITICAL), 0)
    level = ladder.update(_inputs(), 120_000)
    assert level == AlertLevel.L3_DANGER


def test_resolve_emergency_leaves_l3(config):
    ladder = AlertLadder(config)
    ladder.update(_inputs(d3=CRITICAL), 0)
    ladder.resolve_emergency(100)
    assert ladder.level == AlertLevel.L0_NORMAL


def test_sensor_lost_never_maps_to_l0(config):
    """TC-FUS-018 (DOM-FLT-002)"""
    ladder = AlertLadder(config)
    level = ladder.update(_inputs(sensor_lost=True), 0)
    assert level != AlertLevel.L0_NORMAL
    assert level == AlertLevel.L1_EARLY


def test_worked_timeline_from_spec_03_section_6_6(config):
    """TC-FUS-019, a subset: reproduces the escalation shape (not exact
    seconds) from specs/03-drowsiness-domain-spec.md §6.6."""
    ladder = AlertLadder(config)
    assert ladder.update(_inputs(), 0) == AlertLevel.L0_NORMAL
    assert ladder.update(_inputs(d2=ACTIVE), 47_000) == AlertLevel.L1_EARLY
    assert (
        ladder.update(_inputs(d2=ACTIVE, d3=ACTIVE), 90_000) == AlertLevel.L2_DROWSY
    )
    ladder.acknowledge(96_000)
    assert ladder.level == AlertLevel.L0_NORMAL
    assert ladder.update(_inputs(d3=SEVERE), 184_000) == AlertLevel.L2_DROWSY
    assert (
        ladder.update(_inputs(d3=SEVERE), 194_000) == AlertLevel.L3_DANGER
    )
