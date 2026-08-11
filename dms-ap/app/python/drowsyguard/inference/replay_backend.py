"""ReplayBackend -- feeds pre-recorded FrameObservations back through the
exact same domains/fusion code the live backend drives.

specs/02-development-standards.md DEV-044 / DEV-042: this is what lets a
30-minute annotated corpus run through production decision logic
deterministically, in well under a second, with no camera and no
accelerator -- the mechanism specs/06-test-plan.md §2 calls "the highest-
value test in the project". Not wired to a real corpus format yet (no
corpus has been recorded); the JSONL format below is a starting point, not
a finalized spec 06 §5 corpus schema.
"""

from __future__ import annotations

import json
from collections.abc import Iterator
from pathlib import Path

from ..domains.types import FrameObservation


def _observation_from_dict(d: dict) -> FrameObservation:
    return FrameObservation(
        timestamp_ms=d["timestamp_ms"],
        face_present=d["face_present"],
        face_confidence=d.get("face_confidence", 0.0),
        eye_closed=d.get("eye_closed"),
        eye_confidence=d.get("eye_confidence", 0.0),
        sunglasses_detected=d.get("sunglasses_detected", False),
        mouth_open=d.get("mouth_open"),
        mouth_confidence=d.get("mouth_confidence", 0.0),
        yaw_deg=d.get("yaw_deg"),
        pitch_deg=d.get("pitch_deg"),
        indicator_active=d.get("indicator_active", False),
        indicator_dir=d.get("indicator_dir", 0),
    )


class ReplayBackend:
    """Not a subclass of InferenceBackend on purpose: `process()` here takes
    no `now_ms` (the recorded timestamp IS the clock), so a replay driver
    calls `frames()` directly instead of going through the shared
    process(now_ms) contract. See app.py for how a live run and a replay
    run each drive the same downstream pipeline differently at this one
    seam."""

    def __init__(self, path: Path | str) -> None:
        self._path = Path(path)

    def frames(self) -> Iterator[FrameObservation]:
        with open(self._path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                yield _observation_from_dict(json.loads(line))
