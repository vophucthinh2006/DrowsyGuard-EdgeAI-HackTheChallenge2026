"""
Wrapper cho face_landmarks_detector.tflite.

Input:  [1, 256, 256, 3] float32, giá trị [0, 1]
Output:
  Identity   [1,1,1,1434] -> 478 điểm mốc (x, y, z), x/y tính theo pixel
                              của ảnh crop 256x256, z là độ sâu tương đối.
  Identity_1 [1,1,1,1]    -> logit "có khuôn mặt hay không" (qua sigmoid)
  Identity_2 [1,1]        -> nhánh phụ, không dùng ở đây.
"""
import cv2
import numpy as np

from tflite_backend import Interpreter

INPUT_SIZE = 256
NUM_LANDMARKS = 478


def _sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


class FaceLandmarkDetector:
    def __init__(self, model_path="models/face_landmarks_detector.tflite", num_threads=4):
        self.interpreter = Interpreter(model_path=model_path, num_threads=num_threads)
        self.interpreter.allocate_tensors()
        self.input_details = self.interpreter.get_input_details()
        self.output_details = self.interpreter.get_output_details()

    @staticmethod
    def _expand_bbox(bbox, frame_shape, scale=1.6):
        """Mở rộng bbox từ face detector để lấy trọn khuôn mặt + biên (giống
        FaceLandmarksFromPoseCalculator / rect_transformation trong graph gốc)."""
        h_img, w_img = frame_shape[:2]
        x1, y1, x2, y2 = bbox
        cx, cy = (x1 + x2) / 2, (y1 + y2) / 2
        side = max(x2 - x1, y2 - y1) * scale
        nx1, ny1 = cx - side / 2, cy - side / 2
        nx2, ny2 = cx + side / 2, cy + side / 2
        nx1, ny1 = max(0, nx1), max(0, ny1)
        nx2, ny2 = min(w_img, nx2), min(h_img, ny2)
        return int(nx1), int(ny1), int(nx2), int(ny2)

    def predict(self, frame_rgb, bbox):
        """Trả về (landmarks Nx3 theo pixel gốc, face_score) hoặc (None, score)."""
        rx1, ry1, rx2, ry2 = self._expand_bbox(bbox, frame_rgb.shape)
        crop = frame_rgb[ry1:ry2, rx1:rx2]
        if crop.size == 0:
            return None, 0.0

        crop_h, crop_w = crop.shape[:2]
        resized = cv2.resize(crop, (INPUT_SIZE, INPUT_SIZE))
        inp = (resized.astype(np.float32) / 255.0)[None, ...]

        self.interpreter.set_tensor(self.input_details[0]["index"], inp)
        self.interpreter.invoke()

        raw = self.interpreter.get_tensor(self.output_details[0]["index"]).reshape(NUM_LANDMARKS, 3)
        score_logit = self.interpreter.get_tensor(self.output_details[1]["index"]).reshape(-1)[0]
        face_score = float(_sigmoid(score_logit))

        # map tu khong gian 256x256 cua vung crop -> pixel cua khung hinh goc
        landmarks = raw.copy()
        landmarks[:, 0] = landmarks[:, 0] / INPUT_SIZE * crop_w + rx1
        landmarks[:, 1] = landmarks[:, 1] / INPUT_SIZE * crop_h + ry1
        landmarks[:, 2] = landmarks[:, 2] / INPUT_SIZE * max(crop_w, crop_h)

        return landmarks, face_score

    @staticmethod
    def landmarks_to_bbox(landmarks, margin=0.25):
        """Suy bbox cho frame ke tiep tu landmark frame hien tai — dung khi
        bat tracking (khong chay lai face_detector.tflite moi frame)."""
        x1, y1 = landmarks[:, 0].min(), landmarks[:, 1].min()
        x2, y2 = landmarks[:, 0].max(), landmarks[:, 1].max()
        w, h = x2 - x1, y2 - y1
        return (x1 - w * margin, y1 - h * margin, x2 + w * margin, y2 + h * margin)
