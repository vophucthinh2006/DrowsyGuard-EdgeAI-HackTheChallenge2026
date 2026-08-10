import cv2
import mediapipe as mp
import numpy as np
import os
from mediapipe.tasks.python.vision import FaceLandmarker, FaceLandmarkerOptions, RunningMode
from mediapipe.tasks.python.core import base_options

# Chỉ số Landmark giữ nguyên theo bản cập nhật của bạn
LEFT_EYE_IDX = [33, 7, 163, 144, 145, 153, 154, 155, 133, 246, 161, 160, 159, 158, 157, 173]
RIGHT_EYE_IDX = [362, 382, 381, 380, 374, 373, 390, 249, 263, 466, 388, 387, 386, 385, 384, 398]
LEFT_IRIS_IDX = [468, 469, 470, 471]
RIGHT_IRIS_IDX = [472, 473, 474, 475]
LIPS_IDX = [
    61, 146, 91, 181, 84, 17, 314, 405, 321, 375, 291,
    78, 95, 88, 178, 87, 14, 317, 402, 318, 324, 308,
    185, 40, 39, 37, 0, 267, 269, 270, 409
]
KEEP_IDXS = set(LEFT_EYE_IDX + RIGHT_EYE_IDX + LEFT_IRIS_IDX + RIGHT_IRIS_IDX + LIPS_IDX)

class InferenceBackend:
    def __init__(self, model_path: str = '../models/face_landmarker.task'):
        if not os.path.exists(model_path):
            raise FileNotFoundError(f"[ERROR] Model not found at {model_path}")
        
        options = FaceLandmarkerOptions(
            base_options=base_options.BaseOptions(model_asset_path=model_path),
            running_mode=RunningMode.VIDEO,
            output_face_blendshapes=False,
            output_facial_transformation_matrixes=True, # Bật lại để lấy Yaw/Pitch
            num_faces=1
        )
        self.landmarker = FaceLandmarker.create_from_options(options)

    def process_frame(self, frame: np.ndarray, timestamp_ms: int):
        """Pure function: Nhận ảnh + timestamp, trả về raw result"""
        rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_frame)
        
        result = self.landmarker.detect_for_video(mp_image, timestamp_ms)
        return result

    @staticmethod
    def draw_debug_mesh(image: np.ndarray, result) -> np.ndarray:
        """Hàm vẽ debug tái sử dụng nguyên bản code vẽ mống mắt của bạn"""
        if not result.face_landmarks:
            return image
            
        h, w = image.shape[:2]
        face_landmarks = result.face_landmarks[0]

        left_iris_pts = [face_landmarks[i] for i in LEFT_IRIS_IDX if i < len(face_landmarks)]
        right_iris_pts = [face_landmarks[i] for i in RIGHT_IRIS_IDX if i < len(face_landmarks)]

        for idx in KEEP_IDXS:
            if idx in LEFT_IRIS_IDX or idx in RIGHT_IRIS_IDX: continue
            if idx < len(face_landmarks):
                lm = face_landmarks[idx]
                cv2.circle(image, (int(lm.x * w), int(lm.y * h)), 2, (0, 255, 0), -1)

        def _draw_center(pts, color=(0, 0, 255), radius=3):
            if not pts: return
            cx = int(sum(p.x for p in pts) / len(pts) * w)
            cy = int(sum(p.y for p in pts) / len(pts) * h)
            cv2.circle(image, (cx, cy), radius, color, -1)

        _draw_center(left_iris_pts)
        _draw_center(right_iris_pts)
        return image