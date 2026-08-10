"""D2 -- Yawning (ngap). specs/03-drowsiness-domain-spec.md §4.

Operates on `FrameObservation.mouth_open: bool | None`, a per-frame
classification already produced by the inference backend -- NOT a Mouth
Aspect Ratio value thresholded against `mar_open` here. See the note on
`mar_open` in config/thresholds.yaml: the implemented prototype backend
classifies mouth state with a CNN, not landmark geometry, so there is no MAR
to threshold in this module today. If a landmark-based backend is added
later, its MAR-vs-`mar_open` comparison belongs in that backend (or a new
pure helper here fed a raw MAR float) -- either way, D2's event-duration
state machine below does not change.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from ..config import D2Config
from .types import DomainState, FrameObservation


@dataclass
class _YawnEvent:
    start_ms: float
    end_ms: float

    @property
    def duration_ms(self) -> float:
        return self.end_ms - self.start_ms


@dataclass
class D2Yawn:
    config: D2Config

    _state: DomainState = field(default=DomainState.IDLE, init=False)
    _mouth_open_since_ms: float | None = field(default=None, init=False)
    _events: list[_YawnEvent] = field(default_factory=list, init=False)
    _degraded: bool = field(default=False, init=False)

    @property
    def state(self) -> DomainState:
        return self._state

    @property
    def model_degraded(self) -> bool:
        """DOM-D2-002: set for the duration of an over-long episode."""
        return self._degraded

    @property
    def event_count(self) -> int:
        """For DMS_METRICS.yawn_count (spec 04 §4) -- events currently in the window."""
        return len(self._events)

    def _prune_window(self, now_ms: float) -> None:
        window_start = now_ms - self.config.window_ms
        self._events = [e for e in self._events if e.end_ms > window_start]

    def update(self, obs: FrameObservation, now_ms: float) -> DomainState:
        if obs.mouth_open is None:
            pass  # hold, same rationale as D1/D3 -- no evidence this frame
        elif obs.mouth_open:
            if self._mouth_open_since_ms is None:
                self._mouth_open_since_ms = now_ms
            episode_ms = now_ms - self._mouth_open_since_ms
            self._degraded = episode_ms > self.config.yawn_max_ms  # DOM-D2-002
        else:
            if self._mouth_open_since_ms is not None:
                episode_ms = now_ms - self._mouth_open_since_ms
                # DOM-D2-001: only [yawn_min_ms, yawn_max_ms] registers as an
                # event, at the END of the episode.
                if self.config.yawn_min_ms <= episode_ms <= self.config.yawn_max_ms:
                    self._events.append(_YawnEvent(self._mouth_open_since_ms, now_ms))
            self._mouth_open_since_ms = None
            self._degraded = False

        self._prune_window(now_ms)
        count = len(self._events)
        any_long = any(e.duration_ms >= self.config.severe_single_ms for e in self._events)

        # DOM-D2-003/004/005. No explicit "hysteresis" requirement here
        # (unlike D1/fusion) -- the window itself provides it: an event only
        # leaves the count when it ages out.
        if count >= self.config.severe_count or (
            count >= self.config.active_count and any_long
        ):
            self._state = DomainState.SEVERE
        elif count >= self.config.active_count:
            self._state = DomainState.ACTIVE
        else:
            self._state = DomainState.IDLE

        return self._state

    def acknowledge(self) -> None:
        """DOM-D2-007: explicitly a no-op. Ack silences the alert in fusion,
        it must never reset the yawn window -- kept here, doing nothing, so
        the contract is visible rather than implied by omission."""
        return
