from drowsyguard.link import icd


def test_dms_status_round_trips_every_field_at_its_extremes():
    """No literal round-trip decoder exists on this side (DMS_STATUS is
    TX-only from the DMS), so this test decodes with the same bit layout by
    hand to prove the encoder is internally consistent, mirroring
    TC-CAN-005's intent for the VCS-side encode/decode pair."""
    status = icd.DmsStatus(
        alert_level=icd.AlertLevel.L3_DANGER,
        seq=15,
        d1_state=icd.DomainWireState.SEVERE,
        d2_state=icd.DomainWireState.ACTIVE,
        d3_state=icd.DomainWireState.CRITICAL,
        d3_avail=icd.D3Availability.DEGRADED,
        perclos_pct=100,
        eye_closure_ms=65534,
        face_conf_pct=100,
        flag_ack_refractory=True,
        flag_sensor_lost=True,
        flag_model_degraded=True,
        flag_night_mode=True,
        flag_calib_done=True,
        flag_pipeline_slow=True,
        flag_ack_saturated=True,
    )
    payload = icd.encode_dms_status(status)

    assert len(payload) == 8
    assert payload[0] & 0x0F == int(icd.AlertLevel.L3_DANGER)
    assert (payload[0] >> 4) & 0x0F == 15
    assert payload[1] & 0x03 == int(icd.DomainWireState.SEVERE)
    assert (payload[1] >> 2) & 0x03 == int(icd.DomainWireState.ACTIVE)
    assert (payload[1] >> 4) & 0x03 == int(icd.DomainWireState.CRITICAL)
    assert (payload[1] >> 6) & 0x03 == int(icd.D3Availability.DEGRADED)
    assert payload[2] == 100
    assert payload[3] | (payload[4] << 8) == 65534  # little-endian (CAN-010)
    assert payload[5] == 100
    assert payload[6] == 0x7F  # all 7 flag bits set
    from drowsyguard.link.crc8 import crc8

    assert payload[7] == crc8(payload[:7])


def test_vcs_status_decode_rejects_bad_crc():
    payload = bytearray(8)
    payload[0] = int(icd.VehicleState.RUN)
    payload[7] = 0x00  # deliberately wrong CRC
    assert icd.decode_vcs_status(bytes(payload)) is None


def test_vcs_status_decode_rejects_wrong_dlc():
    assert icd.decode_vcs_status(bytes(7)) is None


def test_vcs_status_round_trip():
    """Builds a VCS_STATUS payload the way the VCS firmware would (mirrors
    DG_EncodeVcsStatus in drowsyguard_vcs/src/icd/icd.c) and decodes it."""
    from drowsyguard.link.crc8 import crc8

    payload = bytearray(8)
    payload[0] = int(icd.VehicleState.LIMITED) | (7 << 4)
    payload[1] = 80  # speed_cap_pct
    payload[2] = 60 | 0x80  # duty_left_pct=60, dir_left_reverse=True
    payload[3] = 45  # duty_right_pct=45, dir_right_reverse=False
    payload[4] = 0x01 | 0x10  # fault_driver + estop_active
    payload[5] = 0x34
    payload[6] = 0x12  # uptime_s = 0x1234
    payload[7] = crc8(bytes(payload[:7]))

    decoded = icd.decode_vcs_status(bytes(payload))
    assert decoded is not None
    assert decoded.vehicle_state == icd.VehicleState.LIMITED
    assert decoded.seq == 7
    assert decoded.speed_cap_pct == 80
    assert decoded.duty_left_pct == 60
    assert decoded.dir_left_reverse is True
    assert decoded.duty_right_pct == 45
    assert decoded.dir_right_reverse is False
    assert decoded.fault_driver is True
    assert decoded.estop_active is True
    assert decoded.fault_watchdog_reset is False
    assert decoded.uptime_s == 0x1234


def test_emergency_stop_round_trip():
    payload = icd.encode_emergency_stop(icd.EstopReason.OPERATOR)
    assert payload == bytes([int(icd.EstopReason.OPERATOR), icd.ESTOP_MAGIC])
    assert icd.decode_emergency_stop(payload) == icd.EstopReason.OPERATOR


def test_emergency_stop_rejects_bad_magic():
    """CAN-051: a spurious short frame at this ID must not be able to stop
    the vehicle -- the magic byte is the whole point."""
    payload = bytes([int(icd.EstopReason.OPERATOR), 0x00])
    assert icd.decode_emergency_stop(payload) is None


def test_vcs_event_round_trip():
    payload = bytes([int(icd.EventId.ACK), 42])
    assert icd.decode_vcs_event(payload) == (icd.EventId.ACK, 42)


def test_vcs_event_rejects_unknown_event_id():
    payload = bytes([0xFF, 1])
    assert icd.decode_vcs_event(payload) is None
