"""InferenceBackend interface. specs/02-development-standards.md DEV-044:
everything above the backend (domains, fusion, app orchestration) is
testable with no camera and no accelerator, because the backend is the only
thing that touches a frame, a model, or an accelerator, and it sits behind
this one abstraction."""

from __future__ import annotations

from abc import ABC, abstractmethod

from ..domains.types import FrameObservation


class InferenceBackend(ABC):
    @abstractmethod
    def process(self, now_ms: float) -> FrameObservation | None:
        """Captures/advances one frame and returns its FrameObservation, or
        None if no new frame was available (e.g. camera not yet ready,
        replay corpus exhausted). Never blocks longer than one frame
        period."""

    def close(self) -> None:
        """Release camera/model resources. Default no-op for backends that
        don't hold any (e.g. ReplayBackend)."""
        return
