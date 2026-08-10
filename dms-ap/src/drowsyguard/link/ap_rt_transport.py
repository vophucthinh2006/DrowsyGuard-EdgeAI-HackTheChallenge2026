"""AP<->RT transport: how DMS-AP (this Linux app) hands a DMS_STATUS payload
to DMS-RT (the STM32U585 co-processor, which owns the actual FDCAN
peripheral per specs/04-interface-control-document.md §1.1 and puts the
frame on the physical DMS<->VCS bus).

**This is an interface stub, not a working link.** See README.md in this
directory for the full explanation. In one line: the mechanism the Arduino
UNO Q uses to bridge its Linux core and its MCU core has not been
established in this project yet, so nothing here can be implemented for
real without guessing at an API that might not match the hardware -- exactly
the mistake specs/02-development-standards.md's anti-patterns section warns
against, applied to a new class of interface instead of a threshold.

Everything above this module (fusion, domains, the app loop) only depends on
`ApRtTransport`, never on how a concrete transport works -- so plugging in
the real mechanism later is a one-file change here plus one subclass.
"""

from __future__ import annotations

from abc import ABC, abstractmethod

from .icd import DmsMetrics, DmsStatus, EstopReason, VcsStatus


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
    """Does nothing. Used by app.py when no real transport is configured,
    so the vision/domain/fusion pipeline can still run (and be watched on
    screen) with a stub reporting no vehicle feedback ever arriving --
    exactly the "CAN frame injection" test mode specs/06-test-plan.md §3.2
    describes for the VCS side, mirrored here for the DMS side."""

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
