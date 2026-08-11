from drowsyguard.config import DEFAULT_THRESHOLDS_PATH, load_thresholds


def test_default_thresholds_file_loads_and_matches_spec_03():
    """Spot-checks a handful of values against the normative table in
    specs/03-drowsiness-domain-spec.md §8 -- not a substitute for the real
    tools/check_thresholds.py CI check specs/02 DEV-051 wants (doesn't exist
    yet), but catches a YAML typo or a renamed field immediately."""
    t = load_thresholds(DEFAULT_THRESHOLDS_PATH)

    assert t.d1.active_dwell_ms == 2000
    assert t.d1.severe_cum_ms == 6000
    assert t.d2.yawn_min_ms == 1500
    assert t.d2.active_count == 2
    assert t.d3.active_dwell_ms == 800
    assert t.d3.severe_dwell_ms == 1500
    assert t.d3.critical_dwell_ms == 3000
    assert t.d3.perclos_active == 0.08
    assert t.d3.perclos_severe == 0.15
    assert t.fusion.l2_escalate_ms == 10000
    assert t.fusion.ack_max_consecutive == 3
    assert t.fault.sensor_lost_ms == 3000
