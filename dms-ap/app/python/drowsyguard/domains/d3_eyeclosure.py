"""D3 -- Eye closure (nham mat). specs/03-drowsiness-domain-spec.md §5.

Two independent sub-signals, per spec 03 §5.3 fused as their MAXIMUM
(DOM-D3-005), with CRITICAL reachable ONLY through sub-signal (a)
(DOM-D3-006):

  (a) continuous closure -- immediate escalation once a dwell threshold is
      crossed, but de-escalation requires `clear_dwell_ms` of confirmed OPEN
      eyes first (DOM-D3-007's "not clearable by ack, only by continuously
      open eyes" rule, applied consistently to ACTIVE/SEVERE too -- the more
      conservative reading, and this project's whole ethos is "slow to
      relax, quick to warn").
  (b) PERCLOS -- P80 fraction over a rolling window, with its own
      hysteresis margin on de-escalation (DOM-D3-004).

`eye_closed` here is already a binary classification from the inference
backend (the CNN's own 0.5 decision boundary), not a continuous closure
percentage -- so PERCLOS below is computed as "fraction of valid frames
classified closed", a documented proxy for the literature's P80 definition
rather than a literal re-implementation of it. See inference backend docs.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field

from ..config import D3Config
from .types import DomainState, FrameObservation

_UNAVAILABLE_WINDOW_MS = 3000.0
_UNAVAILABLE_INVALID_FRACTION = 0.30  # DOM-D3-008


@dataclass
class D3EyeClosure:
    config: D3Config

    _a_state: DomainState = field(default=DomainState.IDLE, init=False)
    _b_state: DomainState = field(default=DomainState.IDLE, init=False)
    _closed_since_ms: float | None = field(default=None, init=False)
    _open_since_ms: float | None = field(default=None, init=False)
    _perclos_samples: deque[tuple[float, bool]] = field(default_factory=deque, init=False)
    _validity_history: deque[tuple[float, bool]] = field(default_factory=deque, init=False)
    _unavailable: bool = field(default=False, init=False)
    _model_degraded: bool = field(default=False, init=False)

    @property
    def state(self) -> DomainState:
        """DOM-D3-008: SHALL NOT report IDLE while UNAVAILABLE."""
        effective = DomainState(max(self._a_state, self._b_state))
        if self._unavailable and effective == DomainState.IDLE:
            return DomainState.ACTIVE
        return effective

    @property
    def unavailable(self) -> bool:
        return self._unavailable

    @property
    def model_degraded(self) -> bool:
        return self._unavailable or self._model_degraded

    def continuous_closed_ms(self, now_ms: float) -> float:
        """For DMS_STATUS.eye_closure_ms (spec 04 §3)."""
        return now_ms - self._closed_since_ms if self._closed_since_ms is not None else 0.0

    @property
    def perclos(self) -> float | None:
        """None = INVALID (fewer than perclos_min_samples in the window)."""
        n = len(self._perclos_samples)
        if n < self.config.perclos_min_samples:
            return None
        return sum(1 for _, closed in self._perclos_samples if closed) / n

    def _prune(self, now_ms: float) -> None:
        perclos_start = now_ms - self.config.perclos_window_ms
        while self._perclos_samples and self._perclos_samples[0][0] <= perclos_start:
            self._perclos_samples.popleft()

        unavail_start = now_ms - _UNAVAILABLE_WINDOW_MS
        while self._validity_history and self._validity_history[0][0] <= unavail_start:
            self._validity_history.popleft()

    def _update_availability(self, valid: bool, sunglasses: bool, now_ms: float) -> None:
        self._validity_history.append((now_ms, valid))
        self._prune(now_ms)
        total = len(self._validity_history)
        invalid_fraction = (
            sum(1 for _, v in self._validity_history if not v) / total if total else 0.0
        )
        self._unavailable = sunglasses or invalid_fraction > _UNAVAILABLE_INVALID_FRACTION

    def _update_a_state(self, now_ms: float) -> None:
        continuous_closed_ms = (
            now_ms - self._closed_since_ms if self._closed_since_ms is not None else 0.0
        )
        open_run_ms = now_ms - self._open_since_ms if self._open_since_ms is not None else 0.0

        if continuous_closed_ms >= self.config.critical_dwell_ms:
            candidate = DomainState.CRITICAL
        elif continuous_closed_ms >= self.config.severe_dwell_ms:
            candidate = DomainState.SEVERE
        elif continuous_closed_ms >= self.config.active_dwell_ms:
            candidate = DomainState.ACTIVE
        else:
            candidate = DomainState.IDLE

        if candidate > self._a_state:
            self._a_state = candidate  # DOM-D3-002: immediate escalation
        elif candidate < self._a_state and open_run_ms >= self.config.clear_dwell_ms:
            self._a_state = candidate  # DOM-D3-007: only after confirmed-open dwell

    def _update_b_state(self) -> None:
        value = self.perclos
        if value is None:
            return  # DOM-D3-003: INVALID PERCLOS contributes to no state -- hold

        if value >= self.config.perclos_severe:
            candidate = DomainState.SEVERE
        elif value >= self.config.perclos_active:
            candidate = DomainState.ACTIVE
        else:
            candidate = DomainState.IDLE

        if candidate > self._b_state:
            self._b_state = candidate
        elif candidate < self._b_state:
            # DOM-D3-004: de-escalation needs to clear the threshold by
            # perclos_hyst, not just cross it.
            hyst = self.config.perclos_hyst
            if self._b_state == DomainState.SEVERE and value >= self.config.perclos_severe - hyst:
                return
            if self._b_state == DomainState.ACTIVE and value >= self.config.perclos_active - hyst:
                return
            self._b_state = candidate

    def update(self, obs: FrameObservation, now_ms: float) -> DomainState:
        valid = obs.eye_closed is not None and obs.eye_confidence >= self.config.eye_conf_min
        self._update_availability(valid, obs.sunglasses_detected, now_ms)

        if valid:
            if obs.eye_closed:
                if self._closed_since_ms is None:
                    self._closed_since_ms = now_ms
                self._open_since_ms = None
            else:
                self._closed_since_ms = None
                if self._open_since_ms is None:
                    self._open_since_ms = now_ms
            self._perclos_samples.append((now_ms, bool(obs.eye_closed)))
        # else: DOM-D3-001 -- low-confidence frame HOLDS every accumulator,
        # neither resetting nor incrementing, and is not a PERCLOS sample.

        self._prune(now_ms)
        self._update_a_state(now_ms)
        self._update_b_state()

        return self.state
