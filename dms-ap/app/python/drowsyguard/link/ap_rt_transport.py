"""AP<->RT transport: how DMS-AP (this Linux app) hands a DMS_STATUS payload
to DMS-RT (the STM32U585 co-processor, which owns the actual FDCAN
peripheral per specs/04-interface-control-document.md §1.1 and puts the
frame on the physical DMS<->VCS bus).

Everything above this module (fusion, domains, the app loop) only depends on
`ApRtTransport`, never on how a concrete transport works -- so plugging in a
different mechanism, or fixing a wrong assumption below, is a one-class
change here, nothing upstream moves.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections import deque

from .icd import (
    DmsMetrics,
    DmsStatus,
    EstopReason,
    VcsStatus,
    decode_vcs_event,
    decode_vcs_status,
    encode_dms_metrics,
    encode_dms_status,
    encode_emergency_stop,
)


class ApRtTransport(ABC):
    """What the app loop needs from whatever the real AP<->RT link turns
    out to be. Every method takes/returns already-decoded ICD types
    (link/icd.py) -- this class is only responsible for getting bytes
    across the AP<->RT boundary and back, not for understanding them."""

    @abstractmethod
    def send_dms_status(self, status: DmsStatus) -> None: ...

    @abstractmethod
    def send_dms_metrics(self, metrics: DmsMetrics) -> None: ...

    @abstractmethod
    def send_emergency_stop(self, reason: EstopReason) -> None: ...

    @abstractmethod
    def poll_vcs_status(self) -> VcsStatus | None:
        """Non-blocking: returns the latest VCS_STATUS if a new one has
        arrived since the last call, else None."""

    @abstractmethod
    def poll_events(self) -> list[tuple[int, int]]:
        """Non-blocking: returns any newly-arrived (event_id, event_seq)
        pairs from VCS_EVENT, already de-duplicated on event_seq
        (CAN-040 -- the VCS sends each one 3x)."""


class NullTransport(ApRtTransport):
    """Does nothing. Used by main.py's bench mode when no real transport is
    configured, so the vision/domain/fusion pipeline can still run (and be
    watched on screen) with a stub reporting no vehicle feedback ever
    arriving -- exactly the "CAN frame injection" test mode
    specs/06-test-plan.md §3.2 describes for the VCS side, mirrored here for
    the DMS side."""

    def send_dms_status(self, status: DmsStatus) -> None:
        return

    def send_dms_metrics(self, metrics: DmsMetrics) -> None:
        return

    def send_emergency_stop(self, reason: EstopReason) -> None:
        return

    def poll_vcs_status(self) -> VcsStatus | None:
        return None

    def poll_events(self) -> list[tuple[int, int]]:
        return []


class RouterBridgeTransport(ApRtTransport):
    """Real AP<->RT transport, built on the Arduino UNO Q's own
    **Arduino_RouterBridge** (MessagePack-RPC over the internal serial line
    between the QRB2210/Linux side and the STM32U585/sketch side, brokered
    by App Lab's router service). This is the mechanism link/README.md
    previously said was "not established in this project" -- it turned out
    to be a documented, first-class part of the UNO Q platform:
    https://github.com/arduino/arduino-router,
    `from arduino.app_utils import *` on the Python side,
    `#include <Arduino_RouterBridge.h>` on the sketch side.

    **What is verified vs assumed, exactly:**
      - VERIFIED (from Arduino's own examples/docs): `Bridge.provide(name,
        func)` registers a Python function the sketch can invoke by name;
        `Bridge.call(name, data)` invokes a sketch-registered function; a
        `"linux_started"` readiness handshake is the documented pattern for
        avoiding the sketch racing Python's startup.
      - **ASSUMED, not yet run against a real device** (none available in
        this environment): that `Bridge.call()` accepts a plain `list[int]`
        payload for an 8-byte ICD frame and that MessagePack carries it
        through unchanged to the sketch's `Bridge.call()` handler as
        something iterable/indexable in the same order. If the real API
        wants `bytes` instead, or a dict, or has a size limit below 8
        bytes, this needs a one-function fix in `_send()` below, not a
        redesign -- every call site already goes through it.
    """

    def __init__(self) -> None:
        from arduino.app_utils import Bridge  # type: ignore[import-not-found]

        self._bridge = Bridge
        self._vcs_status_queue: deque[VcsStatus] = deque(maxlen=4)
        self._event_queue: deque[tuple[int, int]] = deque(maxlen=16)
        self._seen_event_seqs: deque[int] = deque(maxlen=8)  # CAN-040 de-dup

        # Readiness handshake the sketch is documented to wait on before
        # its own setup() proceeds -- see the class docstring.
        self._bridge.provide("linux_started", lambda: True)
        self._bridge.provide("vcs_status", self._on_vcs_status)
        self._bridge.provide("vcs_event", self._on_vcs_event)

    def _send(self, channel: str, payload: bytes) -> None:
        self._bridge.call(channel, list(payload))

    def send_dms_status(self, status: DmsStatus) -> None:
        self._send("dms_status", encode_dms_status(status))

    def send_dms_metrics(self, metrics: DmsMetrics) -> None:
        self._send("dms_metrics", encode_dms_metrics(metrics))

    def send_emergency_stop(self, reason: EstopReason) -> None:
        self._send("emergency_stop", encode_emergency_stop(reason))

    def _on_vcs_status(self, data: list[int]) -> None:
        """Registered with Bridge.provide(); the sketch calls this by name
        (Bridge.call("vcs_status", ...) on the .ino side) whenever a fresh
        VCS_STATUS arrives off FDCAN1."""
        decoded = decode_vcs_status(bytes(data))
        if decoded is not None:
            self._vcs_status_queue.append(decoded)

    def _on_vcs_event(self, data: list[int]) -> None:
        decoded = decode_vcs_event(bytes(data))
        if decoded is None:
            return
        event_id, event_seq = decoded
        if event_seq in self._seen_event_seqs:
            return  # CAN-040: the VCS sends each event 3x, de-duplicate
        self._seen_event_seqs.append(event_seq)
        self._event_queue.append((int(event_id), event_seq))

    def poll_vcs_status(self) -> VcsStatus | None:
        if not self._vcs_status_queue:
            return None
        return self._vcs_status_queue.pop()  # latest wins; drop any backlog

    def poll_events(self) -> list[tuple[int, int]]:
        drained = list(self._event_queue)
        self._event_queue.clear()
        return drained
