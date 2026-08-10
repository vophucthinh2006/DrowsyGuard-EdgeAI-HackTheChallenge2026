"""Fusion: domain states -> the L0-L3 alert ladder. specs/03 §6.

Two simplifications from the spec text, both documented at the point they
matter below rather than left implicit:

  - DOM-FUS-003's "no intervening return to a *sustained* L0" is implemented
    as: the ack counter (for a 10-minute rolling window) is reset once the
    level has genuinely been IDLE-driven back to L0 (not merely touched it).
  - DOM-FUS-005's per-level de-escalation restarts the `level_clear_ms`
    timer after each step, so a longer IDLE run steps down multiple levels,
    one `level_clear_ms` apart, rather than all at once.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from enum import IntEnum

from ..config import FusionConfig
from ..domains.types import DomainState


class AlertLevel(IntEnum):
    L0_NORMAL = 0
    L1_EARLY = 1
    L2_DROWSY = 2
    L3_DANGER = 3


@dataclass
class FusionInputs:
    d1: DomainState
    d2: DomainState
    d3: DomainState
    sensor_lost: bool  # specs/03 §7 -- fault, never silently mapped to L0
    d3_unavailable: bool


@dataclass
class AlertLadder:
    config: FusionConfig

    _level: AlertLevel = field(default=AlertLevel.L0_NORMAL, init=False)
    _l2_since_ms: float | None = field(default=None, init=False)
    _all_idle_since_ms: float | None = field(default=None, init=False)
    _ack_refractory_until_ms: float = field(default=float("-inf"), init=False)
    _ack_saturated: bool = field(default=False, init=False)
    _ack_timestamps_ms: deque[float] = field(default_factory=deque, init=False)
    _last_l0_reached: bool = field(default=True, init=False)  # starts at L0

    @property
    def level(self) -> AlertLevel:
        return self._level

    @property
    def ack_saturated(self) -> bool:
        return self._ack_saturated

    def _domain_driven_level(self, inputs: FusionInputs) -> AlertLevel:
        states = (inputs.d1, inputs.d2, inputs.d3)

        if inputs.d3 == DomainState.CRITICAL:
            return AlertLevel.L3_DANGER  # DOM-000 / spec 03 §6.1

        severe_count = sum(1 for s in states if s == DomainState.SEVERE)
        active_count = sum(1 for s in states if s == DomainState.ACTIVE)

        if severe_count >= 1 or (active_count + severe_count) >= 2:
            return AlertLevel.L2_DROWSY  # spec 03 §6.1/6.2
        if active_count == 1:
            return AlertLevel.L1_EARLY
        return AlertLevel.L0_NORMAL

    def _fault_floor(self, inputs: FusionInputs) -> AlertLevel:
        """DOM-FLT-002: a fault SHALL NEVER be silently mapped to L0."""
        if inputs.sensor_lost or inputs.d3_unavailable:
            return AlertLevel.L1_EARLY
        return AlertLevel.L0_NORMAL

    def _prune_ack_window(self, now_ms: float) -> None:
        window_start = now_ms - 600_000.0  # 10 minutes, DOM-FUS-003
        while self._ack_timestamps_ms and self._ack_timestamps_ms[0] <= window_start:
            self._ack_timestamps_ms.popleft()

    def update(self, inputs: FusionInputs, now_ms: float) -> AlertLevel:
        drowsy_level = self._domain_driven_level(inputs)
        fault_level = self._fault_floor(inputs)

        # DOM-FUS-001: the post-ack refractory suppresses an L1 re-entry
        # caused by drowsiness evidence only -- a fault (sensor lost, D3
        # unavailable) is never silenced by an unrelated ack (DOM-FLT-002's
        # "never silently mapped to L0" spirit extends to "never silenced").
        if drowsy_level == AlertLevel.L1_EARLY and self.l1_suppressed(now_ms):
            drowsy_level = AlertLevel.L0_NORMAL
        domain_level = max(drowsy_level, fault_level)

        if self._level == AlertLevel.L3_DANGER:
            # DOM-FUS-006: never auto de-escalates. Only resolve_emergency()
            # (an explicit operator re-arm from the VCS) can leave L3.
            return self._level

        if domain_level > self._level:
            self._level = domain_level  # immediate escalation, DOM-FUS-005
        elif domain_level < self._level:
            if domain_level == AlertLevel.L0_NORMAL:
                if self._all_idle_since_ms is None:
                    self._all_idle_since_ms = now_ms
                if now_ms - self._all_idle_since_ms >= self.config.level_clear_ms:
                    self._level = AlertLevel(self._level - 1)  # one step at a time
                    self._all_idle_since_ms = now_ms  # restart for the next step
            else:
                self._all_idle_since_ms = None
                # A drop that isn't all the way to domain_level==L0 (e.g.
                # SEVERE->ACTIVE) still only steps down one level, same rule.
                self._level = AlertLevel(self._level - 1)
        else:
            self._all_idle_since_ms = None if domain_level != AlertLevel.L0_NORMAL else (
                self._all_idle_since_ms
            )

        if self._level == AlertLevel.L0_NORMAL:
            if not self._last_l0_reached:
                self._ack_timestamps_ms.clear()  # DOM-FUS-003 "sustained" reset
            self._last_l0_reached = True
        else:
            self._last_l0_reached = False

        # DOM-000 / SYS-FR-020: L2 -> L3 after level_2_escalate_ms with no ack.
        if self._level == AlertLevel.L2_DROWSY:
            if self._l2_since_ms is None:
                self._l2_since_ms = now_ms
            elif now_ms - self._l2_since_ms >= self.config.l2_escalate_ms:
                self._level = AlertLevel.L3_DANGER
        else:
            self._l2_since_ms = None

        return self._level

    def acknowledge(self, now_ms: float) -> None:
        """DOM-FUS-001/002/003. No effect at L3 (DOM-D3-007 extends to
        fusion: the button cannot silence the one signal that means the
        driver may be unconscious)."""
        if self._level == AlertLevel.L3_DANGER:
            return

        self._prune_ack_window(now_ms)
        self._ack_timestamps_ms.append(now_ms)
        if len(self._ack_timestamps_ms) > self.config.ack_max_consecutive:
            self._ack_saturated = True
        else:
            self._ack_saturated = False
            self._ack_refractory_until_ms = now_ms + self.config.ack_refractory_ms

        if self._level in (AlertLevel.L1_EARLY, AlertLevel.L2_DROWSY):
            self._level = AlertLevel.L0_NORMAL
            self._all_idle_since_ms = None
            self._l2_since_ms = None

    def resolve_emergency(self, now_ms: float) -> None:
        """Explicit operator re-arm from the VCS (VEH-012/SYS-FR-033) --
        the only way out of L3 (DOM-FUS-006)."""
        self._level = AlertLevel.L0_NORMAL
        self._all_idle_since_ms = now_ms
        self._l2_since_ms = None

    def l1_suppressed(self, now_ms: float) -> bool:
        """DOM-FUS-001: whether an L1 re-entry is currently suppressed by
        the post-ack refractory. L2 is never suppressed (DOM-FUS-001)."""
        return now_ms < self._ack_refractory_until_ms and not self._ack_saturated
