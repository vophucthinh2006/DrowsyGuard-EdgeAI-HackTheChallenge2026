"""Optional on-screen debug view -- the visual half of the original
`Custom_Blaze_Face+CNN/demo.py` prototype, unchanged in behaviour, moved
here so it's opt-in (`app.py --preview`) rather than baked into the
inference loop. Never used on the actual in-vehicle deployment (no display),
only for bench development.
"""

from __future__ import annotations

import cv2
import numpy as np

from ..domains.types import FrameObservation
from ..fusion.ladder import AlertLevel

_LEVEL_COLOR = {
    AlertLevel.L0_NORMAL: (0, 200, 0),
    AlertLevel.L1_EARLY: (0, 200, 200),
    AlertLevel.L2_DROWSY: (0, 100, 255),
    AlertLevel.L3_DANGER: (0, 0, 255),
}


def draw_overlay(frame: np.ndarray, obs: FrameObservation, level: AlertLevel) -> np.ndarray:
    color = _LEVEL_COLOR[level]
    cv2.rectangle(frame, (0, 0), (frame.shape[1] - 1, 40), (30, 30, 30), -1)
    cv2.putText(
        frame,
        f"Level: {level.name}",
        (10, 28),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.8,
        color,
        2,
    )

    if not obs.face_present:
        cv2.putText(frame, "NO FACE", (10, 70), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)
        return frame

    if obs.eye_closed is not None:
        eye_str = f"eyes: {'CLOSED' if obs.eye_closed else 'OPEN'} ({obs.eye_confidence:.2f})"
        cv2.putText(frame, eye_str, (10, 70), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

    if obs.mouth_open is not None:
        mouth_str = f"mouth: {'OPEN' if obs.mouth_open else 'CLOSED'} ({obs.mouth_confidence:.2f})"
        cv2.putText(frame, mouth_str, (10, 95), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)

    return frame
