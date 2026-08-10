"""The real inference backend: BlazeFace (MediaPipe) face detection + two
tiny CNNs (eye open/closed, mouth open/yawn) on cropped regions.

Refactored out of the original `Custom_Blaze_Face+CNN/demo.py` prototype --
same models, same crop geometry, same classification logic. What changed is
packaging: this file now returns a `FrameObservation` (the pure interface
`domains/` and `fusion/` consume) instead of drawing directly on a frame and
looping forever, and the OpenCV window/live demo logic moved to
`inference/debug_overlay.py` + `app.py` so this class can also be swapped in
by `tools/live_preview.py` or run headless.

**Known gaps, carried over honestly rather than silently fixed by assuming
a value:**
  - `yaw_deg` / `pitch_deg` are always `None`. BlazeFace's 6 keypoints alone
    don't give a validated head-pose estimate without a 3D face model +
    camera intrinsics (solvePnP), which this prototype never built. D1
    (distraction) treats `None` as "no evidence this frame" and simply does
    not detect distraction yet -- see domains/d1_distraction.py.
  - `sunglasses_detected` is always `False` -- no classifier for it exists.
  - Confidence values (`eye_confidence`, `mouth_confidence`) are a distance-
    from-decision-boundary proxy (`abs(prob - 0.5) * 2`), not a calibrated
    confidence estimate. This determines whether D3's EYE_CONF_MIN gate
    (specs/03 §5.1, DOM-D3-001) holds or accumulates a frame, so it is worth
    re-examining once real corpus data exists to check it behaves sensibly
    (specs/03 DOM-TUN-001..004 tuning protocol applies here too, even though
    it isn't a `thresholds.yaml` value).
"""

from __future__ import annotations

from pathlib import Path

import cv2
import mediapipe as mp
import numpy as np
import tensorflow as tf
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision as mp_vision

from ..capture.camera import Camera
from ..domains.types import FrameObservation
from .backend import InferenceBackend

EYE_CROP_SIZE = (96, 48)  # (W, H)
MOUTH_CROP_SIZE = (64, 64)
MIN_DETECTION_CONFIDENCE = 0.60


def _clamp_crop(image: np.ndarray, center_x: float, center_y: float, width: float, height: float):
    image_height, image_width = image.shape[:2]
    x1 = max(0, int(center_x - width / 2))
    y1 = max(0, int(center_y - height / 2))
    x2 = min(image_width, int(center_x + width / 2))
    y2 = min(image_height, int(center_y + height / 2))
    if x2 <= x1 or y2 <= y1:
        return None
    return image[y1:y2, x1:x2]


def _get_eye_crops(frame: np.ndarray, detection) -> list[np.ndarray]:
    box = detection.bounding_box
    keypoints = detection.keypoints
    if len(keypoints) < 2:
        return []

    crop_width = max(40, box.width * 0.42)
    crop_height = max(24, box.height * 0.22)
    frame_height, frame_width = frame.shape[:2]
    crops = []
    for point in keypoints[:2]:
        crop = _clamp_crop(
            frame, point.x * frame_width, point.y * frame_height, crop_width, crop_height
        )
        if crop is not None:
            crops.append(cv2.resize(crop, EYE_CROP_SIZE, interpolation=cv2.INTER_AREA))
    return crops


def _get_mouth_crop(frame: np.ndarray, detection) -> np.ndarray | None:
    box = detection.bounding_box
    keypoints = detection.keypoints
    if len(keypoints) < 4:
        return None

    mouth_kp = keypoints[3]  # MediaPipe FaceDetector keypoint index 3 = mouth center
    frame_height, frame_width = frame.shape[:2]
    crop_width = max(48, box.width * 0.50)
    crop_height = max(48, box.height * 0.40)

    crop = _clamp_crop(
        frame, mouth_kp.x * frame_width, mouth_kp.y * frame_height, crop_width, crop_height
    )
    if crop is None:
        return None
    return cv2.resize(crop, MOUTH_CROP_SIZE, interpolation=cv2.INTER_AREA)


def _predict(crop: np.ndarray, interpreter, input_details, output_details) -> float:
    gray = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)
    _, target_h, target_w, _ = input_details[0]["shape"]
    resized = cv2.resize(gray, (target_w, target_h), interpolation=cv2.INTER_AREA)
    input_tensor = resized.astype(np.float32)[np.newaxis, ..., np.newaxis]
    interpreter.set_tensor(input_details[0]["index"], input_tensor)
    interpreter.invoke()
    return float(interpreter.get_tensor(output_details[0]["index"])[0][0])


def _confidence(prob: float) -> float:
    """Distance from the 0.5 decision boundary, scaled to [0, 1]. See the
    module docstring "Known gaps" note before trusting this."""
    return abs(prob - 0.5) * 2.0


class BlazeFaceCnnBackend(InferenceBackend):
    def __init__(self, models_dir: Path, camera: Camera) -> None:
        blaze_face_path = models_dir / "blaze_face_short_range.tflite"
        eye_cnn_path = models_dir / "tiny_eyecnn_v2_float32.tflite"
        yawn_cnn_path = models_dir / "tiny_mouthcnn_v1_float32.tflite"

        for p in (blaze_face_path, eye_cnn_path, yawn_cnn_path):
            if not p.is_file():
                raise FileNotFoundError(f"missing model file: {p}")

        self._camera = camera

        self._eye_interpreter = tf.lite.Interpreter(model_path=str(eye_cnn_path))
        self._eye_interpreter.allocate_tensors()
        self._eye_input = self._eye_interpreter.get_input_details()
        self._eye_output = self._eye_interpreter.get_output_details()

        self._yawn_interpreter = tf.lite.Interpreter(model_path=str(yawn_cnn_path))
        self._yawn_interpreter.allocate_tensors()
        self._yawn_input = self._yawn_interpreter.get_input_details()
        self._yawn_output = self._yawn_interpreter.get_output_details()

        options = mp_vision.FaceDetectorOptions(
            base_options=mp_python.BaseOptions(model_asset_path=str(blaze_face_path)),
            running_mode=mp_vision.RunningMode.VIDEO,
            min_detection_confidence=MIN_DETECTION_CONFIDENCE,
        )
        self._detector = mp_vision.FaceDetector.create_from_options(options)

        self._last_frame: np.ndarray | None = None
        self._last_detection = None  # for debug_overlay.py

    def process(self, now_ms: float) -> FrameObservation | None:
        frame = self._camera.read()
        if frame is None:
            return None
        self._last_frame = frame

        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_frame)
        result = self._detector.detect_for_video(mp_image, int(now_ms))

        if not result.detections:
            self._last_detection = None
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

        detection = result.detections[0]
        self._last_detection = detection
        face_confidence = detection.categories[0].score if detection.categories else 1.0

        eye_closed: bool | None = None
        eye_confidence = 0.0
        eye_crops = _get_eye_crops(frame, detection)
        if eye_crops:
            # Two eyes classified independently; take the lower open-probability
            # (more-closed) eye so one droopy eye isn't masked by the other,
            # matching how a human observer would read "eyes closed".
            probs = [
                _predict(crop, self._eye_interpreter, self._eye_input, self._eye_output)
                for crop in eye_crops
            ]
            min_prob = min(probs)
            eye_closed = min_prob <= 0.5
            eye_confidence = _confidence(min_prob)

        mouth_open: bool | None = None
        mouth_confidence = 0.0
        mouth_crop = _get_mouth_crop(frame, detection)
        if mouth_crop is not None:
            prob = _predict(mouth_crop, self._yawn_interpreter, self._yawn_input, self._yawn_output)
            mouth_open = prob > 0.5
            mouth_confidence = _confidence(prob)

        return FrameObservation(
            timestamp_ms=now_ms,
            face_present=True,
            face_confidence=face_confidence,
            eye_closed=eye_closed,
            eye_confidence=eye_confidence,
            sunglasses_detected=False,
            mouth_open=mouth_open,
            mouth_confidence=mouth_confidence,
            yaw_deg=None,
            pitch_deg=None,
        )

    @property
    def last_frame(self) -> np.ndarray | None:
        """For inference/debug_overlay.py -- not part of the InferenceBackend contract."""
        return self._last_frame

    @property
    def last_detection(self):
        return self._last_detection

    def close(self) -> None:
        self._camera.release()
        self._detector.close()
