"""Real-time Face Detection, Eye State & Yawn Classification using BlazeFace + Tiny_EyeCNN."""

from __future__ import annotations

import time
from pathlib import Path

import cv2
import numpy as np
import mediapipe as mp
from mediapipe.tasks import python
from mediapipe.tasks.python import vision
import tensorflow as tf

CAMERA_INDEX = 0
MIN_DETECTION_CONFIDENCE = 0.60
EYE_CROP_SIZE = (96, 48)     # Crop size for eyes from BlazeFace (W, H)
MOUTH_CROP_SIZE = (64, 64)   # Crop size for mouth ROI (W, H)
MODEL_INPUT_SIZE = (64, 64)  # Input tensor size for Tiny_EyeCNN / Tiny_YawnCNN (W, H)

# Model file paths
BLAZE_FACE_PATH = Path("models/blaze_face_short_range.tflite")
EYE_CNN_PATH = Path("models/tiny_eyecnn_v2_float32.tflite")
YAWN_CNN_PATH = Path("models/tiny_mouthcnn_v1_float32.tflite")


def clamp_crop(image, center_x: float, center_y: float, width: float, height: float):
    """Crop image region around center coordinates with boundary checks."""
    image_height, image_width = image.shape[:2]
    x1 = max(0, int(center_x - width / 2))
    y1 = max(0, int(center_y - height / 2))
    x2 = min(image_width, int(center_x + width / 2))
    y2 = min(image_height, int(center_y + height / 2))

    if x2 <= x1 or y2 <= y1:
        return None
    return image[y1:y2, x1:x2]


def get_eye_crops(frame, detection):
    """Crop left and right eye regions based on BlazeFace keypoints [0] and [1]."""
    box = detection.bounding_box
    keypoints = detection.keypoints
    if len(keypoints) < 2:
        return []

    crop_width = max(40, box.width * 0.42)
    crop_height = max(24, box.height * 0.22)
    frame_height, frame_width = frame.shape[:2]
    crops = []
    for point in keypoints[:2]:
        crop = clamp_crop(
            frame,
            point.x * frame_width,
            point.y * frame_height,
            crop_width,
            crop_height,
        )
        if crop is not None:
            crops.append(cv2.resize(crop, EYE_CROP_SIZE, interpolation=cv2.INTER_AREA))
    return crops


def get_mouth_crop(frame, detection):
    """Crop mouth region based on BlazeFace keypoint [3] (mouth center) with padding."""
    box = detection.bounding_box
    keypoints = detection.keypoints

    # Keypoint [3] in MediaPipe FaceDetector corresponds to Mouth Center
    if len(keypoints) < 4:
        return None

    mouth_kp = keypoints[3]
    frame_height, frame_width = frame.shape[:2]

    # Include extra padding (+20%) around mouth to prevent clipping during wide yawns
    crop_width = max(48, box.width * 0.50)
    crop_height = max(48, box.height * 0.40)

    crop = clamp_crop(
        frame,
        mouth_kp.x * frame_width,
        mouth_kp.y * frame_height,
        crop_width,
        crop_height,
    )

    if crop is not None:
        return cv2.resize(crop, MOUTH_CROP_SIZE, interpolation=cv2.INTER_AREA)
    return None


def predict_roi_state(crop, interpreter, input_details, output_details) -> float:
    """Preprocess grayscale ROI crop and run TFLite inference dynamically matching model shape."""
    # 1. Convert to grayscale
    gray = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)

    # 2. Lấy kích thước đầu vào mong muốn trực tiếp từ TFLite Model
    # Shape dạng [batch, height, width, channels] -> e.g. [1, 32, 32, 1] hoặc [1, 64, 64, 1]
    _, target_h, target_w, _ = input_details[0]['shape']

    # 3. Resize về đúng kích thước mô hình yêu cầu (W, H)
    resized = cv2.resize(gray, (target_w, target_h), interpolation=cv2.INTER_AREA)

    # 4. Format input tensor in [0, 255] float32 range
    input_tensor = resized.astype(np.float32)[np.newaxis, ..., np.newaxis]

    # 5. Run TFLite inference
    interpreter.set_tensor(input_details[0]['index'], input_tensor)
    interpreter.invoke()

    # 6. Extract Sigmoid probability output
    prediction = interpreter.get_tensor(output_details[0]['index'])[0][0]
    return float(prediction)


def draw_eye_panel(frame, crops, predictions) -> None:
    """Render top-right panel displaying eye crops, prediction labels, and confidence."""
    if not crops:
        return

    frame_height, frame_width = frame.shape[:2]
    label_height = 22
    margin = 8
    tile_width, tile_height = EYE_CROP_SIZE[0], EYE_CROP_SIZE[1] + label_height
    panel_width = len(crops) * tile_width + (len(crops) + 1) * margin
    start_x = max(margin, frame_width - panel_width)
    start_y = margin

    for index, crop in enumerate(crops):
        x = start_x + margin + index * (tile_width + margin)
        if x + tile_width > frame_width or start_y + tile_height > frame_height:
            return

        tile = cv2.copyMakeBorder(
            crop, label_height, 0, 0, 0, cv2.BORDER_CONSTANT, value=(30, 30, 30)
        )
        frame[start_y:start_y + tile_height, x:x + tile_width] = tile

        prob = predictions[index] if index < len(predictions) else 0.0

        # Class indexing: 0 = CLOSED, 1 = OPEN
        state_str = "OPEN" if prob > 0.5 else "CLOSED"
        color = (0, 255, 0) if prob > 0.5 else (0, 0, 255)

        eye_label = f"{'R' if index == 0 else 'L'}: {state_str} ({prob:.2f})"
        cv2.putText(frame, eye_label, (x + 2, start_y + 15), cv2.FONT_HERSHEY_SIMPLEX,
                    0.35, color, 1)
        cv2.rectangle(frame, (x, start_y), (x + tile_width, start_y + tile_height),
                      color, 1)


def draw_mouth_panel(frame, mouth_crop, prediction: float | None) -> None:
    """Render top-left panel displaying mouth crop, yawn label, and confidence."""
    if mouth_crop is None or prediction is None:
        return

    label_height = 22
    start_x, start_y = 10, 10
    tile_width, tile_height = MOUTH_CROP_SIZE[0], MOUTH_CROP_SIZE[1] + label_height

    tile = cv2.copyMakeBorder(
        mouth_crop, label_height, 0, 0, 0, cv2.BORDER_CONSTANT, value=(30, 30, 30)
    )
    frame[start_y:start_y + tile_height, start_x:start_x + tile_width] = tile

    # Class indexing: 0 = NO YAWN, 1 = YAWN
    state_str = "YAWN" if prediction > 0.1 else "NO YAWN"
    color = (0, 0, 255) if prediction > 0.1 else (0, 255, 0)

    mouth_label = f"{state_str} ({prediction:.2f})"
    cv2.putText(frame, mouth_label, (start_x + 2, start_y + 15),
                cv2.FONT_HERSHEY_SIMPLEX, 0.35, color, 1)
    cv2.rectangle(frame, (start_x, start_y), (start_x + tile_width, start_y + tile_height),
                  color, 1)


def main() -> None:
    camera = cv2.VideoCapture(CAMERA_INDEX)
    if not camera.isOpened():
        raise RuntimeError("Could not open the webcam.")

    # Validate model file availability
    if not BLAZE_FACE_PATH.is_file() or not EYE_CNN_PATH.is_file() or not YAWN_CNN_PATH.is_file():
        raise FileNotFoundError("One or more .tflite model files are missing from the models/ directory.")

    # 1. Initialize TFLite Interpreter for Eye Model
    eye_interpreter = tf.lite.Interpreter(model_path=str(EYE_CNN_PATH))
    eye_interpreter.allocate_tensors()
    eye_input_details = eye_interpreter.get_input_details()
    eye_output_details = eye_interpreter.get_output_details()

    # 2. Initialize TFLite Interpreter for Yawn Model
    yawn_interpreter = tf.lite.Interpreter(model_path=str(YAWN_CNN_PATH))
    yawn_interpreter.allocate_tensors()
    yawn_input_details = yawn_interpreter.get_input_details()
    yawn_output_details = yawn_interpreter.get_output_details()

    # 3. Initialize MediaPipe BlazeFace Detector
    options = vision.FaceDetectorOptions(
        base_options=python.BaseOptions(model_asset_path=str(BLAZE_FACE_PATH)),
        running_mode=vision.RunningMode.VIDEO,
        min_detection_confidence=MIN_DETECTION_CONFIDENCE,
    )

    start_time = time.monotonic()
    last_timestamp_ms = -1
    cv2.namedWindow("Drowsiness Detection System", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("Drowsiness Detection System", 960, 720)

    with vision.FaceDetector.create_from_options(options) as face_detector:
        while True:
            success, frame = camera.read()
            if not success:
                break

            frame = cv2.flip(frame, 1)
            rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_frame)

            timestamp_ms = int((time.monotonic() - start_time) * 1000)
            timestamp_ms = max(timestamp_ms, last_timestamp_ms + 1)
            last_timestamp_ms = timestamp_ms

            result = face_detector.detect_for_video(mp_image, timestamp_ms)
            eye_crops = []
            eye_predictions = []
            mouth_crop = None
            yawn_prediction = None

            if result.detections:
                detection = result.detections[0]
                box = detection.bounding_box
                cv2.rectangle(frame, (box.origin_x, box.origin_y),
                              (box.origin_x + box.width, box.origin_y + box.height),
                              (0, 255, 0), 2)

                # Crop eyes and predict eye states (Open/Closed)
                eye_crops = get_eye_crops(frame, detection)
                for crop in eye_crops:
                    prob = predict_roi_state(crop, eye_interpreter, eye_input_details, eye_output_details)
                    eye_predictions.append(prob)

                # Crop mouth region and predict yawn state (Yawn/No Yawn)
                mouth_crop = get_mouth_crop(frame, detection)
                if mouth_crop is not None:
                    yawn_prediction = predict_roi_state(
                        mouth_crop, yawn_interpreter, yawn_input_details, yawn_output_details
                    )

            # Draw visual panels on video frame
            draw_eye_panel(frame, eye_crops, eye_predictions)
            draw_mouth_panel(frame, mouth_crop, yawn_prediction)

            cv2.imshow("Drowsiness Detection System", frame)

            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

    camera.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()