"""DrowsyGuard CAN Interface Control Document -- wire format.

Byte-exact implementation of ../../../../shared/icd/icd.yaml (the canonical,
machine-readable message catalogue) and specs/04-interface-control-document.md.
This is the Python side of the ICD; ../../../../vcs-mcxn947/src/icd/icd.h is
the C side. `shared/icd/generate.py` does not exist yet (specs/02 DEV-002 is
still open -- see ../../../../shared/icd/README.md for exactly what that
means), so this file is hand-written and hand-kept in sync with the C side
rather than generated. Any field change here SHALL also be made in icd.yaml
and icd.h/icd.c in the same commit (DEV-092).

Transport note: this module only does wire encode/decode. It does NOT open
a CAN socket. The CAN controller in the DMS is on the STM32U585 (DMS-RT),
not reachable directly from Linux (DMS-AP) -- see link/README.md for why
`ap_rt_transport.py` is an interface stub, not a working link, and what is
still unknown about it.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum

from .crc8 import crc8

# ---- CAN identifiers (spec 04 §2) -----------------------------------------
CANID_EMERGENCY_STOP = 0x080
CANID_DMS_STATUS = 0x100
CANID_DMS_METRICS = 0x101
CANID_VCS_STATUS = 0x200
CANID_VCS_EVENT = 0x201
CANID_DIAG_REQ = 0x700
CANID_DIAG_RESP = 0x701

ESTOP_MAGIC = 0x5A


class AlertLevel(IntEnum):
    L0_NORMAL = 0
    L1_EARLY = 1
    L2_DROWSY = 2
    L3_DANGER = 3


class DomainWireState(IntEnum):
    IDLE = 0
    ACTIVE = 1
    SEVERE = 2
    CRITICAL = 3  # D3 only


class D3Availability(IntEnum):
    AVAILABLE = 0
    DEGRADED = 1
    UNAVAILABLE = 2


class VehicleState(IntEnum):
    INIT = 0
    DISARMED = 1
    ARMED_IDLE = 2
    RUN = 3
    LIMITED = 4
    DECEL = 5
    STOPPED = 6
    LINK_LOST = 7
    FAULT = 8
    ESTOP = 9


class EventId(IntEnum):
    ACK = 1
    OPERATOR_REARM = 2
    ESTOP_ASSERTED = 3
    ESTOP_RELEASED = 4
    INDICATOR_ON = 5
    INDICATOR_OFF = 6


class EstopReason(IntEnum):
    PHYSICAL = 1
    DMS_FAULT = 2
    VCS_FAULT = 3
    OPERATOR = 4


# ---- 0x100 DMS_STATUS (TX from this node) ---------------------------------


@dataclass(frozen=True, slots=True)
class DmsStatus:
    alert_level: AlertLevel
    seq: int  # 4-bit, 0..15, caller increments

    d1_state: DomainWireState
    d2_state: DomainWireState
    d3_state: DomainWireState
    d3_avail: D3Availability

    perclos_pct: int  # 0..100, 255 = INVALID
    eye_closure_ms: int  # 0..65534, 65535 = not measurable
    face_conf_pct: int  # 0..100, 255 = no face

    flag_ack_refractory: bool = False
    flag_sensor_lost: bool = False
    flag_model_degraded: bool = False
    flag_night_mode: bool = False
    flag_calib_done: bool = False
    flag_pipeline_slow: bool = False
    flag_ack_saturated: bool = False


def encode_dms_status(status: DmsStatus) -> bytes:
    payload = bytearray(8)
    payload[0] = (int(status.alert_level) & 0x0F) | ((status.seq & 0x0F) << 4)
    payload[1] = (
        (int(status.d1_state) & 0x03)
        | ((int(status.d2_state) & 0x03) << 2)
        | ((int(status.d3_state) & 0x03) << 4)
        | ((int(status.d3_avail) & 0x03) << 6)
    )
    payload[2] = status.perclos_pct & 0xFF
    payload[3] = status.eye_closure_ms & 0xFF
    payload[4] = (status.eye_closure_ms >> 8) & 0xFF
    payload[5] = status.face_conf_pct & 0xFF
    payload[6] = (
        (0x01 if status.flag_ack_refractory else 0)
        | (0x02 if status.flag_sensor_lost else 0)
        | (0x04 if status.flag_model_degraded else 0)
        | (0x08 if status.flag_night_mode else 0)
        | (0x10 if status.flag_calib_done else 0)
        | (0x20 if status.flag_pipeline_slow else 0)
        | (0x40 if status.flag_ack_saturated else 0)
    )
    payload[7] = crc8(bytes(payload[:7]))
    return bytes(payload)


# ---- 0x101 DMS_METRICS (TX from this node), telemetry only ---------------


@dataclass(frozen=True, slots=True)
class DmsMetrics:
    fps_x10: int
    inference_ms: int
    yawn_count: int
    eor_cum_ms: int
    dropped_pct: int
    seq: int  # 8-bit, free-running


def encode_dms_metrics(metrics: DmsMetrics) -> bytes:
    payload = bytearray(8)
    payload[0] = min(metrics.fps_x10, 255) & 0xFF
    payload[1] = min(metrics.inference_ms, 255) & 0xFF
    payload[2] = metrics.yawn_count & 0xFF
    payload[3] = metrics.eor_cum_ms & 0xFF
    payload[4] = (metrics.eor_cum_ms >> 8) & 0xFF
    payload[5] = metrics.dropped_pct & 0xFF
    payload[6] = metrics.seq & 0xFF
    payload[7] = crc8(bytes(payload[:7]))
    return bytes(payload)


# ---- 0x200 VCS_STATUS (RX on this node) -----------------------------------


@dataclass(frozen=True, slots=True)
class VcsStatus:
    vehicle_state: VehicleState
    seq: int
    speed_cap_pct: int
    duty_left_pct: int
    dir_left_reverse: bool
    duty_right_pct: int
    dir_right_reverse: bool
    fault_driver: bool
    fault_watchdog_reset: bool
    fault_can_timeout: bool
    fault_undervoltage: bool
    estop_active: bool
    indicator_active: bool
    indicator_dir: int
    uptime_s: int


def decode_vcs_status(payload: bytes) -> VcsStatus | None:
    if len(payload) != 8:
        return None
    if crc8(payload[:7]) != payload[7]:
        return None

    return VcsStatus(
        vehicle_state=VehicleState(payload[0] & 0x0F),
        seq=(payload[0] >> 4) & 0x0F,
        speed_cap_pct=payload[1],
        duty_left_pct=payload[2] & 0x7F,
        dir_left_reverse=bool(payload[2] & 0x80),
        duty_right_pct=payload[3] & 0x7F,
        dir_right_reverse=bool(payload[3] & 0x80),
        fault_driver=bool(payload[4] & 0x01),
        fault_watchdog_reset=bool(payload[4] & 0x02),
        fault_can_timeout=bool(payload[4] & 0x04),
        fault_undervoltage=bool(payload[4] & 0x08),
        estop_active=bool(payload[4] & 0x10),
        indicator_active=bool(payload[4] & 0x20),
        indicator_dir=(payload[4] >> 6) & 0x03,
        uptime_s=payload[5] | (payload[6] << 8),
    )


# ---- 0x201 VCS_EVENT (RX on this node) ------------------------------------


def decode_vcs_event(payload: bytes) -> tuple[EventId, int] | None:
    """Returns (event_id, event_seq) or None. Per CAN-040 the VCS sends each
    event 3x at 10 ms spacing with the same event_seq -- de-duplication on
    event_seq is the caller's job (link/ap_rt_transport.py), not this pure
    decode function's."""
    if len(payload) != 2:
        return None
    try:
        return EventId(payload[0]), payload[1]
    except ValueError:
        return None


# ---- 0x080 EMERGENCY_STOP (either direction) ------------------------------


def encode_emergency_stop(reason: EstopReason) -> bytes:
    return bytes([int(reason), ESTOP_MAGIC])


def decode_emergency_stop(payload: bytes) -> EstopReason | None:
    if len(payload) != 2 or payload[1] != ESTOP_MAGIC:
        return None  # CAN-051
    try:
        return EstopReason(payload[0])
    except ValueError:
        return None
