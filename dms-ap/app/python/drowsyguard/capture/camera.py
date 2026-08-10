"""Camera abstraction. Kept separate from inference/ so the frame source
(live webcam, day/night switch, a recorded corpus clip for replay capture)
can change without touching model/backend code.

specs/03-drowsiness-domain-spec.md §5.1 assumes a `night_mode` flag the
system can report over CAN (DMS_STATUS byte6 bit3) when IR illumination is
active -- this class exposes `night_mode` as a settable property for that,
but nothing here auto-detects day/night yet (no IR camera integrated into
this prototype); it defaults to False and must be set externally.
"""

from __future__ import annotations

import cv2
import numpy as np


class Camera:
    def __init__(self, index: int = 0) -> None:
        self._capture = cv2.VideoCapture(index)
        if not self._capture.isOpened():
            raise RuntimeError(f"could not open camera index {index}")
        self.night_mode = False

    def read(self) -> np.ndarray | None:
        """Returns a horizontally-mirrored BGR frame (mirrored to match a
        driver-facing camera's natural "looking in a mirror" orientation),
        or None if the capture failed."""
        success, frame = self._capture.read()
        if not success:
            return None
        return cv2.flip(frame, 1)

    def release(self) -> None:
        self._capture.release()
