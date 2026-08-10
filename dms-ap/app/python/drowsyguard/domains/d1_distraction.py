"""D1 -- Distraction (mat tap trung). specs/03-drowsiness-domain-spec.md §3.

Sign convention (this implementation's choice, not fixed by the spec text):
`yaw_deg` positive = head turned toward the passenger side, negative = driver
side. `pitch_deg` positive = head/eyes tilted down. Whatever inference
backend produces these must follow this convention or D1's indicator
suppression (DOM-D1-005) will suppress the wrong direction.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from ..config import D1Config
from .types import DomainState, FrameObservation

_INDICATOR_NONE = 0
_INDICATOR_LEFT = 1
_INDICATOR_RIGHT = 2


@dataclass
class _Segment:
    start_ms: float
    end_ms: float


@dataclass
class D1Distraction:
    """Stateful but pure (DEV-042): update() takes an observation + the
    caller's clock, returns the new state, touches nothing external."""

    config: D1Config

    _state: DomainState = field(default=DomainState.IDLE, init=False)
    _off_road_start_ms: float | None = field(default=None, init=False)
    _on_road_since_ms: float | None = field(default=None, init=False)
    _confirmed_segments: list[_Segment] = field(default_factory=list, init=False)
    _suppress_until_ms: float = field(default=float("-inf"), init=False)
    _suppress_dir: int = field(default=_INDICATOR_NONE, init=False)

    @property
    def state(self) -> DomainState:
        return self._state

    def cumulative_off_road_ms(self, now_ms: float) -> float:
        """For DMS_METRICS.eor_cum_ms (spec 04 §4)."""
        self._prune_window(now_ms)
        return self._cumulative_off_road_ms(now_ms)

    def _off_road_this_frame(self, obs: FrameObservation, now_ms: float) -> bool | None:
        if obs.yaw_deg is None or obs.pitch_deg is None:
            return None  # DOM: no evidence this frame -- see types.py docstring

        if obs.indicator_active:
            self._suppress_until_ms = now_ms + self.config.indicator_suppress_ms
            self._suppress_dir = obs.indicator_dir

        suppressed = now_ms <= self._suppress_until_ms

        yaw_off = abs(obs.yaw_deg) > self.config.yaw_limit_deg
        if yaw_off and suppressed:
            turning_toward_indicated = (
                obs.yaw_deg > 0 and self._suppress_dir == _INDICATOR_RIGHT
            ) or (obs.yaw_deg < 0 and self._suppress_dir == _INDICATOR_LEFT)
            if turning_toward_indicated:
                yaw_off = False  # DOM-D1-005: only the indicated direction is suppressed

        pitch_off = obs.pitch_deg > self.config.pitch_down_limit_deg  # never suppressed

        return yaw_off or pitch_off

    def _prune_window(self, now_ms: float) -> None:
        window_start = now_ms - self.config.cum_window_ms
        self._confirmed_segments = [
            s for s in self._confirmed_segments if s.end_ms > window_start
        ]

    def _cumulative_off_road_ms(self, now_ms: float) -> float:
        window_start = now_ms - self.config.cum_window_ms
        total = 0.0
        for seg in self._confirmed_segments:
            total += max(0.0, min(seg.end_ms, now_ms) - max(seg.start_ms, window_start))
        return total

    def update(self, obs: FrameObservation, now_ms: float) -> DomainState:
        off_road = self._off_road_this_frame(obs, now_ms)

        if off_road is None:
            # No evidence this frame: hold, same spirit as D3's DOM-D3-001 --
            # neither confirms nor cancels a distraction episode in progress.
            pass
        elif off_road:
            if self._off_road_start_ms is None:
                self._off_road_start_ms = now_ms
            self._on_road_since_ms = None

            run_ms = now_ms - self._off_road_start_ms
            if run_ms >= self.config.glance_min_ms:
                # DOM-D1-001: only contributes once past the noise floor;
                # replace any partial segment already recorded for this run.
                if self._confirmed_segments and self._confirmed_segments[-1].start_ms == (
                    self._off_road_start_ms
                ):
                    self._confirmed_segments[-1].end_ms = now_ms
                else:
                    self._confirmed_segments.append(_Segment(self._off_road_start_ms, now_ms))
        else:
            self._off_road_start_ms = None
            if self._on_road_since_ms is None:
                self._on_road_since_ms = now_ms

        self._prune_window(now_ms)
        eor_cum_ms = self._cumulative_off_road_ms(now_ms)
        eor_continuous_ms = (
            now_ms - self._off_road_start_ms if self._off_road_start_ms is not None else 0.0
        )

        # DOM-D1-002/003
        if eor_continuous_ms >= 2 * self.config.active_dwell_ms:
            self._state = DomainState.SEVERE
        elif eor_cum_ms >= self.config.severe_cum_ms:
            self._state = DomainState.SEVERE
        elif eor_continuous_ms >= self.config.active_dwell_ms:
            self._state = DomainState.ACTIVE

        # DOM-D1-004: clear only after clear_dwell_ms continuously on-road
        # AND cumulative has fallen back below severe_cum_ms.
        if self._state != DomainState.IDLE:
            on_road_ms = (
                now_ms - self._on_road_since_ms if self._on_road_since_ms is not None else 0.0
            )
            if on_road_ms >= self.config.clear_dwell_ms and eor_cum_ms < self.config.severe_cum_ms:
                self._state = DomainState.IDLE

        return self._state
