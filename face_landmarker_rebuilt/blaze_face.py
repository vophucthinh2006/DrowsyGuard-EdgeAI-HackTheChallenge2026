"""
Wrapper cho face_detector.tflite (BlazeFace short-range).

Model input:  [1, 128, 128, 3] float32, giá trị [-1, 1]
Model output:
  regressors     [1, 896, 16]  -> 4 box coord + 6 keypoint (x,y) = 16
  classificators [1, 896, 1]   -> điểm số (chưa qua sigmoid)

Đây là phần thay thế cho "face detection subgraph" mà Tasks API giấu bên
trong .task bundle.
"""
import numpy as np

from tflite_backend import Interpreter
from anchors import generate_anchors

INPUT_SIZE = 128
SCORE_THRESH = 0.5
IOU_THRESH = 0.3


def _sigmoid(x):
    x = np.clip(x, -30, 30)  # tranh tran so (overflow) khi tinh exp
    return 1.0 / (1.0 + np.exp(-x))


def _iou(box, boxes):
    x1 = np.maximum(box[0], boxes[:, 0])
    y1 = np.maximum(box[1], boxes[:, 1])
    x2 = np.minimum(box[2], boxes[:, 2])
    y2 = np.minimum(box[3], boxes[:, 3])
    inter = np.maximum(0, x2 - x1) * np.maximum(0, y2 - y1)
    area1 = (box[2] - box[0]) * (box[3] - box[1])
    area2 = (boxes[:, 2] - boxes[:, 0]) * (boxes[:, 3] - boxes[:, 1])
    return inter / (area1 + area2 - inter + 1e-6)


def _nms(boxes, scores, iou_thresh):
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        if order.size == 1:
            break
        ious = _iou(boxes[i], boxes[order[1:]])
        order = order[1:][ious < iou_thresh]
    return keep


class BlazeFaceDetector:
    def __init__(self, model_path="models/face_detector.tflite", num_threads=4):
        # num_threads=4: tan dung du 4 nhan Cortex-A53 tren board dual-chip
        # (Arduino UNO Q / cac board tuong tu), thay vi mac dinh chi 1 luong.
        self.interpreter = Interpreter(model_path=model_path, num_threads=num_threads)
        self.interpreter.allocate_tensors()
        self.input_details = self.interpreter.get_input_details()
        self.output_details = self.interpreter.get_output_details()
        self.anchors = generate_anchors()  # (896, 4) -> cx, cy, w, h (normalized)

    def _preprocess(self, frame_rgb):
        h, w = frame_rgb.shape[:2]
        # letterbox về hình vuông rồi resize 128x128 để giữ đúng tỉ lệ khuôn mặt
        side = max(h, w)
        square = np.zeros((side, side, 3), dtype=np.uint8)
        square[:h, :w] = frame_rgb
        resized = np.asarray(
            __import__("cv2").resize(square, (INPUT_SIZE, INPUT_SIZE))
        )
        inp = (resized.astype(np.float32) / 127.5) - 1.0
        return inp[None, ...], side

    def detect(self, frame_rgb):
        """Trả về list các dict {bbox:(x1,y1,x2,y2) theo pixel gốc, score}."""
        inp, side = self._preprocess(frame_rgb)
        self.interpreter.set_tensor(self.input_details[0]["index"], inp)
        self.interpreter.invoke()

        raw_boxes = self.interpreter.get_tensor(self.output_details[0]["index"])[0]  # (896,16)
        raw_scores = self.interpreter.get_tensor(self.output_details[1]["index"])[0, :, 0]  # (896,)

        scores = _sigmoid(raw_scores)
        mask = scores > SCORE_THRESH
        if not np.any(mask):
            return []

        boxes = raw_boxes[mask]
        scores = scores[mask]
        anchors = self.anchors[mask]

        cx = boxes[:, 0] / INPUT_SIZE + anchors[:, 0]
        cy = boxes[:, 1] / INPUT_SIZE + anchors[:, 1]
        w = boxes[:, 2] / INPUT_SIZE
        h = boxes[:, 3] / INPUT_SIZE

        x1 = (cx - w / 2) * side
        y1 = (cy - h / 2) * side
        x2 = (cx + w / 2) * side
        y2 = (cy + h / 2) * side
        boxes_px = np.stack([x1, y1, x2, y2], axis=1)

        keep = _nms(boxes_px, scores, IOU_THRESH)
        results = []
        for i in keep:
            results.append({"bbox": tuple(boxes_px[i]), "score": float(scores[i])})
        return results
