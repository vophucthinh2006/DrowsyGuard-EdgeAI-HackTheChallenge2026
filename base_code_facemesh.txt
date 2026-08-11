import cv2
import mediapipe as mp
import numpy as np
import time
import os
from mediapipe.tasks.python.vision import FaceLandmarker, FaceLandmarkerOptions, RunningMode
from mediapipe.tasks.python.core import base_options

BaseOptions = base_options.BaseOptions
VisionRunningMode = RunningMode

# Runtime debug/label flags
SHOW_LABELS = False
LABEL_STEP = 10
PRINT_COORDS = False
_last_print_time = 0

# Landmark index groups (MediaPipe Face Mesh canonical indices)
# Expanded to include upper eyelid and upper-lip border points.
# Sources: MediaPipe Face Mesh canonical index mapping (common subsets).
LEFT_EYE_IDX = [33, 7, 163, 144, 145, 153, 154, 155, 133, 246, 161, 160, 159, 158, 157, 173]
RIGHT_EYE_IDX = [362, 382, 381, 380, 374, 373, 390, 249, 263, 466, 388, 387, 386, 385, 384, 398]
LEFT_IRIS_IDX = [468, 469, 470, 471]
RIGHT_IRIS_IDX = [472, 473, 474, 475]
# Expanded lip indices: outer + inner lip contours (MediaPipe canonical subsets)
LIPS_IDX = [
    61, 146, 91, 181, 84, 17, 314, 405, 321, 375, 291,  # outer lip
    78, 95, 88, 178, 87, 14, 317, 402, 318, 324, 308,    # inner lip / border
    185, 40, 39, 37, 0, 267, 269, 270, 409                # additional lip-related points
]

# Keep indices: eyes, irises, and lips (we'll draw iris as a single center later)
KEEP_IDXS = set(LEFT_EYE_IDX + RIGHT_EYE_IDX + LEFT_IRIS_IDX + RIGHT_IRIS_IDX + LIPS_IDX)

# ==========================================
# 1. KHỞI TẠO CÁC TIỆN ÍCH VẼ CỦA MEDIAPIPE
# ==========================================

# ==========================================
# 2. KHỞI TẠO MODEL FACE LANDMARKER (Chế độ VIDEO)
# ==========================================
model_path = 'face_landmarker.task'
if not os.path.exists(model_path):
    print(f"[LOI] Khong tim thay file {model_path}!")
    print("Hay tai tai: https://developers.google.com/mediapipe/solutions/vision/face_landmarker#python-task-api")
    exit()

options = FaceLandmarkerOptions(
    base_options=BaseOptions(model_asset_path=model_path),
    running_mode=VisionRunningMode.VIDEO, 
    output_face_blendshapes=False, # Tắt blendshapes để tối ưu FPS nếu chỉ cần vẽ
    output_facial_transformation_matrixes=False,
    num_faces=1
)

landmarker = FaceLandmarker.create_from_options(options)

# ==========================================
# 3. HÀM VẼ TOÀN BỘ FACE MESH LÊN ẢNH
# ==========================================
def draw_full_mesh(image, detection_result, show_labels=False, label_step=10, print_coords=False):
    face_landmarks_list = detection_result.face_landmarks

    for face_landmarks in face_landmarks_list:
        h, w = image.shape[:2]

        # Prepare iris center points (draw a single center per eye) and draw other kept landmarks
        left_iris_pts = [face_landmarks[i] for i in LEFT_IRIS_IDX if i < len(face_landmarks)]
        right_iris_pts = [face_landmarks[i] for i in RIGHT_IRIS_IDX if i < len(face_landmarks)]

        # Draw kept landmarks except raw iris points (we'll draw iris centers separately)
        for idx in KEEP_IDXS:
            if idx in LEFT_IRIS_IDX or idx in RIGHT_IRIS_IDX:
                continue
            if idx < len(face_landmarks):
                lm = face_landmarks[idx]
                p = (int(lm.x * w), int(lm.y * h))
                cv2.circle(image, p, 2, (0, 255, 0), -1)

        # Helper: draw averaged center of a group of landmarks
        def _draw_center(pts, color=(0, 0, 255), radius=3):
            if not pts:
                return
            cx = int(sum(p.x for p in pts) / len(pts) * w)
            cy = int(sum(p.y for p in pts) / len(pts) * h)
            cv2.circle(image, (cx, cy), radius, color, -1)

        # Draw iris centers in red to distinguish from green mesh points
        _draw_center(left_iris_pts, color=(0, 0, 255), radius=3)
        _draw_center(right_iris_pts, color=(0, 0, 255), radius=3)

        # optional: draw numeric labels for kept indices every label_step (skip raw iris points)
        if show_labels and label_step > 0:
            for idx in sorted(KEEP_IDXS):
                if idx in LEFT_IRIS_IDX or idx in RIGHT_IRIS_IDX:
                    continue
                if idx < len(face_landmarks) and (idx % label_step == 0):
                    lm = face_landmarks[idx]
                    x, y = int(lm.x * w), int(lm.y * h)
                    cv2.putText(image, str(idx), (x + 2, y + 2), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 0, 255), 1)

        # optional: limited console output for a few key indices
        if print_coords:
            global _last_print_time
            if (time.time() - _last_print_time) > 0.5:
                # print only a few representative kept indices (if present)
                key_idxs = [33, 362, 61, 291, 468, 472]
                coords = []
                for k in key_idxs:
                    if 0 <= k < len(face_landmarks):
                        lm = face_landmarks[k]
                        coords.append((k, lm.x, lm.y))
                print('sample landmarks:', coords)
                _last_print_time = time.time()

    return image

# ==========================================
# 4. VÒNG LẶP CAMERA CHÍNH
# ==========================================
cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280) # Để phân giải cao nhìn lưới cho rõ
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

print("[INFO] Dang mo camera. Nhan 'q' de thoat...")

while cap.isOpened():
    ret, frame = cap.read()
    if not ret:
        break

    # Lật ảnh để có hiệu ứng gương
    frame = cv2.flip(frame, 1)
    
    # Chuyển đổi màu cho MediaPipe xử lý
    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb_frame)
    
    # Lấy timestamp để chạy chế độ VIDEO
    timestamp_ms = int(cap.get(cv2.CAP_PROP_POS_MSEC))
    if timestamp_ms == 0:
        timestamp_ms = int(time.time() * 1000)
    
    # Nhận diện
    result = landmarker.detect_for_video(mp_image, timestamp_ms)
    
    # Nếu có khuôn mặt, tiến hành vẽ
    if result.face_landmarks:
        # Gọi hàm vẽ trực tiếp trên frame BGR để tối ưu (tránh copy và convert màu)
        draw_full_mesh(frame, result, show_labels=SHOW_LABELS, label_step=LABEL_STEP, print_coords=PRINT_COORDS)

    # Hiển thị
    cv2.imshow("Full Face Mesh Viewer", frame)

    key = cv2.waitKey(1) & 0xFF
    if key == ord('q'):
        break
    elif key == ord('l'):
        SHOW_LABELS = not SHOW_LABELS
        print('SHOW_LABELS =', SHOW_LABELS)
    elif key == ord('p'):
        PRINT_COORDS = not PRINT_COORDS
        print('PRINT_COORDS =', PRINT_COORDS)
    elif key == ord('['):
        LABEL_STEP = min(max(1, LABEL_STEP-1), 50)
        print('LABEL_STEP =', LABEL_STEP)
    elif key == ord(']'):
        LABEL_STEP = min(max(1, LABEL_STEP+1), 50)
        print('LABEL_STEP =', LABEL_STEP)

cap.release()
cv2.destroyAllWindows()