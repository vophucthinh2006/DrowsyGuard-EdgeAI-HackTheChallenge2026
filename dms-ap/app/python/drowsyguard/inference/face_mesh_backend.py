"""Inference backend using MediaPipe FaceMesh to extract 478 landmarks.
Calculates EAR (Eye Aspect Ratio), MAR (Mouth Aspect Ratio) and
Head Pose (Yaw/Pitch) geometrically.
"""

from __future__ import annotations

import math
from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision as mp_vision

from ..capture.camera import Camera
from ..domains.types import FrameObservation
from .backend import InferenceBackend

# MediaPipe Face Mesh canonical indices
LEFT_EYE = [362, 385, 387, 263, 373, 380]
RIGHT_EYE = [33, 160, 158, 133, 153, 144]
MOUTH_MAR = [78, 308, 13, 14]


def _distance(p1, p2) -> float:
    return math.hypot(p1.x - p2.x, p1.y - p2.y)


def _calculate_eye_ear(eye_indices: list[int], landmarks) -> float:
    p0 = landmarks[eye_indices[0]]
    p1 = landmarks[eye_indices[1]]
    p2 = landmarks[eye_indices[2]]
    p3 = landmarks[eye_indices[3]]
    p4 = landmarks[eye_indices[4]]
    p5 = landmarks[eye_indices[5]]

    A = _distance(p1, p5)
    B = _distance(p2, p4)
    C = _distance(p0, p3)

    return (A + B) / (2.0 * C) if C > 0 else 0.0


def _calculate_mouth_mar(mouth_indices: list[int], landmarks) -> float:
    p_left = landmarks[mouth_indices[0]]
    p_right = landmarks[mouth_indices[1]]
    p_top = landmarks[mouth_indices[2]]
    p_bottom = landmarks[mouth_indices[3]]

    vertical_dist = _distance(p_top, p_bottom)
    horizontal_dist = _distance(p_left, p_right)

    return vertical_dist / horizontal_dist if horizontal_dist > 0 else 0.0


class FaceMeshBackend(InferenceBackend):
    def __init__(self, models_dir: Path, camera: Camera, ear_threshold: float = 0.22, mar_threshold: float = 0.60) -> None:
        model_path = models_dir / "face_landmarker.task"
        if not model_path.is_file():
            raise FileNotFoundError(f"missing model file: {model_path}")

        self._camera = camera
        self.ear_threshold = ear_threshold
        self.mar_threshold = mar_threshold

        options = mp_vision.FaceLandmarkerOptions(
            base_options=mp_python.BaseOptions(model_asset_path=str(model_path)),
            running_mode=mp_vision.RunningMode.VIDEO,
            output_face_blendshapes=False,
            output_facial_transformation_matrixes=True,
            num_faces=1,
        )
        self._landmarker = mp_vision.FaceLandmarker.create_from_options(options)

        self._last_frame: np.ndarray | None = None
        self._last_detection = None

    def process(self, now_ms: float) -> FrameObservation | None:
        frame = self._camera.read()
        if frame is None:
            return None
        self._last_frame = frame

        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_frame)
        result = self._landmarker.detect_for_video(mp_image, int(now_ms))

        self._last_detection = result

        if not result.face_landmarks:
            return FrameObservation(
                timestamp_ms=now_ms,
                face_present=False,
                face_confidence=0.0,
                eye_closed=None,
                eye_confidence=0.0,
                sunglasses_detected=False,
                mouth_open=None,
                mouth_confidence=0.0,
                yaw_deg=None,
                pitch_deg=None,
            )

        face_landmarks = result.face_landmarks[0]

        # EAR calculation
        left_ear = _calculate_eye_ear(LEFT_EYE, face_landmarks)
        right_ear = _calculate_eye_ear(RIGHT_EYE, face_landmarks)
        avg_ear = (left_ear + right_ear) / 2.0
        eye_closed = avg_ear < self.ear_threshold

        # MAR calculation
        mar = _calculate_mouth_mar(MOUTH_MAR, face_landmarks)
        mouth_open = mar > self.mar_threshold

        # Head pose
        yaw_deg: float | None = None
        pitch_deg: float | None = None

        if result.facial_transformation_matrixes:
            matrix = result.facial_transformation_matrixes[0]
            rotation_matrix = matrix[:3, :3]
            euler_angles, _, _, _, _, _ = cv2.RQDecomp3x3(rotation_matrix)
            pitch_deg, yaw_deg, roll_deg = euler_angles

        return FrameObservation(
            timestamp_ms=now_ms,
            face_present=True,
            face_confidence=1.0,
            eye_closed=eye_closed,
            eye_confidence=1.0,
            sunglasses_detected=False,
            mouth_open=mouth_open,
            mouth_confidence=1.0,
            yaw_deg=yaw_deg,
            pitch_deg=pitch_deg,
        )

    @property
    def last_frame(self) -> np.ndarray | None:
        return self._last_frame

    @property
    def last_detection(self):
        return self._last_detection

    def close(self) -> None:
        self._camera.release()
        self._landmarker.close()
